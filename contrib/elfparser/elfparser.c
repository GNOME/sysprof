/* Sysprof -- Sampling, systemwide CPU profiler
 * Copyright 2006, 2007, Soeren Sandmann
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
# include <libelf.h>
#else
# include <elf.h>
#endif
#include <sys/mman.h>

#include "demangle.h"
#include "elfparser.h"
#include "rust-demangle.h"

typedef struct Section Section;

struct ElfSym
{
    gulong table;
    gulong offset;
    gulong address;
};

struct Section
{
    const gchar *       name;
    gsize               offset;
    gsize               size;
    gboolean            allocated;
    gulong              load_address;
    guint               type;
};

struct ElfParser
{
    gboolean            is_64;
    const guchar *      data;
    gsize               length;

    guint               n_sections;
    Section **          sections;

    guint               n_symbols;
    ElfSym *            symbols;
    gsize               sym_strings;

    GMappedFile *       file;

    char *              filename;

    gboolean            checked_build_id;
    char *              build_id;

    const Section *     text_section;
};

/* FIXME: All of these should in principle do endian swapping,
 * but sysprof never has to deal with binaries of a different
 * endianness than sysprof itself
 */
#define GET_FIELD(parser, offset, struct_name, idx, field_name)         \
    (((parser))->is_64?                                                 \
     ((Elf64_ ## struct_name *)(gpointer)(((parser)->data + offset)) + (idx))->field_name : \
     ((Elf32_ ## struct_name *)(gpointer)(((parser)->data + offset)) + (idx))->field_name)

#define GET_UINT32(parser, offset)                                      \
    *((uint32_t *)(gpointer)(parser->data + offset))                    \

#define GET_SIZE(parser, struct_name)                                   \
    (((parser)->is_64?                                                  \
      sizeof (Elf64_ ## struct_name) :                                  \
      sizeof (Elf32_ ## struct_name)))

#define MAKE_ELF_UINT_ACCESSOR(field_name)                              \
    static uint64_t field_name  (ElfParser *parser)                     \
    {                                                                   \
        return GET_FIELD (parser, 0, Ehdr, 0, field_name);              \
    }

MAKE_ELF_UINT_ACCESSOR (e_shoff)
MAKE_ELF_UINT_ACCESSOR (e_shnum)
MAKE_ELF_UINT_ACCESSOR (e_shstrndx)

#define MAKE_SECTION_HEADER_ACCESSOR(field_name)                        \
    static uint64_t field_name (ElfParser *parser, int nth_section)     \
    {                                                                   \
        gsize offset = e_shoff (parser);                                \
                                                                        \
        return GET_FIELD (parser, offset, Shdr, nth_section, field_name); \
    }

MAKE_SECTION_HEADER_ACCESSOR (sh_name);
MAKE_SECTION_HEADER_ACCESSOR (sh_type);
MAKE_SECTION_HEADER_ACCESSOR (sh_flags);
MAKE_SECTION_HEADER_ACCESSOR (sh_addr);
MAKE_SECTION_HEADER_ACCESSOR (sh_offset);
MAKE_SECTION_HEADER_ACCESSOR (sh_size);

#define MAKE_SYMBOL_ACCESSOR(field_name)                                \
    static uint64_t field_name (ElfParser *parser, gulong offset, gulong nth)   \
    {                                                                   \
        return GET_FIELD (parser, offset, Sym, nth, field_name);        \
    }

MAKE_SYMBOL_ACCESSOR(st_name);
MAKE_SYMBOL_ACCESSOR(st_info);
MAKE_SYMBOL_ACCESSOR(st_value);
MAKE_SYMBOL_ACCESSOR(st_size);
MAKE_SYMBOL_ACCESSOR(st_shndx);

static gboolean
in_container (void)
{
    static gboolean _in_container;
    static gboolean initialized;

    if (!initialized)
    {
        /* Flatpak has /.flatpak-info
         * Podman has /run/.containerenv
         *
         * Both have access to host files via /var/run/host.
         */
        _in_container = g_file_test ("/.flatpak-info", G_FILE_TEST_EXISTS) ||
                        g_file_test ("/run/.containerenv", G_FILE_TEST_EXISTS);

        initialized = TRUE;
    }

    return _in_container;
}

static void
section_free (Section *section)
{
    g_free (section);
}

static const Section *
find_section (ElfParser *parser,
              const char *name,
              guint       type)
{
    guint i;

    if (name == NULL)
        return NULL;

    for (i = 0; i < parser->n_sections; ++i)
    {
        Section *section = parser->sections[i];

        if (section->name == NULL || section->type != type)
            continue;

        if (strcmp (section->name, name) == 0)
            return section;
    }

    return NULL;
}

static gboolean
parse_elf_signature (const guchar *data,
                     gsize         length,
                     gboolean     *is_64,
                     gboolean     *is_be)
{
    /* FIXME: this function should be able to return an error */
    if (length < EI_NIDENT)
    {
        /* FIXME set error */
        return FALSE;
    }

    if (data[EI_CLASS] != ELFCLASS32 &&
        data[EI_CLASS] != ELFCLASS64)
    {
        /* FIXME set error */
        return FALSE;
    }

    if (data[EI_DATA] != ELFDATA2LSB &&
        data[EI_DATA] != ELFDATA2MSB)
    {
        /* FIXME set error */
        return FALSE;
    }

    if (is_64)
        *is_64 = (data[EI_CLASS] == ELFCLASS64);

    if (is_be)
        *is_be = (data[EI_DATA] == ELFDATA2MSB);

    return TRUE;
}

ElfParser *
elf_parser_new_from_data (const guchar *data,
                          gsize length)
{
    ElfParser *parser;
    gboolean is_64, is_big_endian;
    int section_names_idx;
    const guchar *section_names;
    G_GNUC_UNUSED gsize section_headers;
    guint i;

    if (!parse_elf_signature (data, length, &is_64, &is_big_endian))
    {
        /* FIXME: set error */
        return NULL;
    }

    parser = g_new0 (ElfParser, 1);

    parser->is_64 = is_64;
    parser->data = data;
    parser->length = length;

#if 0
    g_print ("  new parser : %p\n", parser);
#endif

    /* Read ELF header */

    parser->n_sections = e_shnum (parser);
    section_names_idx = e_shstrndx (parser);
    section_headers = e_shoff (parser);

    /* Read section headers */
    parser->sections = g_new0 (Section *, parser->n_sections);

    section_names = parser->data + sh_offset (parser, section_names_idx);

    for (i = 0; i < parser->n_sections; ++i)
    {
        Section *section = g_new (Section, 1);

        section->name = (char *)(section_names + sh_name (parser, i));
        section->size = sh_size (parser, i);
        section->offset = sh_offset (parser, i);
        section->allocated = !!(sh_flags (parser, i) & SHF_ALLOC);

        if (section->allocated)
            section->load_address = sh_addr (parser, i);
        else
            section->load_address = 0;

        section->type = sh_type (parser, i);

        parser->sections[i] = section;
    }

    /* Cache the text section */
    parser->text_section = find_section (parser, ".text", SHT_PROGBITS);
    if (!parser->text_section)
        parser->text_section = find_section (parser, ".text", SHT_NOBITS);

    parser->filename = NULL;
    parser->build_id = NULL;

    return parser;
}

static GMappedFile *
open_mapped_file (const char  *filename,
                  GError     **error)
{
    GMappedFile *file = NULL;
    char *alternate = NULL;

    if (in_container () && !g_str_has_prefix (filename, g_get_home_dir ()))
      {
        alternate = g_build_filename ("/var/run/host", filename, NULL);
        file = g_mapped_file_new (alternate, FALSE, NULL);
        g_free (alternate);
      }

    /* Flatpaks with filesystem=host don't have Silverblue's /sysroot in /var/run/host,
     * yet they have it in /, which means the original path might work.
     */
    if (!file)
      file = g_mapped_file_new (filename, FALSE, error);

    return file;
}

ElfParser *
elf_parser_new_from_mmap (GMappedFile  *file,
                          GError      **error)
{
    const guchar *data;
    gsize length;
    ElfParser *parser;

    if (file == NULL)
      return NULL;

    data = (guchar *)g_mapped_file_get_contents (file);
    length = g_mapped_file_get_length (file);
    parser = elf_parser_new_from_data (data, length);

    if (!parser)
    {
        g_set_error (error,
                     G_FILE_ERROR,
                     G_FILE_ERROR_FAILED,
                     "Failed to load ELF from mmap region");
        g_mapped_file_unref (file);
        return NULL;
    }

    parser->filename = NULL;
    parser->file = file;

    return parser;
}

ElfParser *
elf_parser_new (const char  *filename,
                GError     **error)
{
    GMappedFile *file;
    const guchar *data;
    gsize length;
    ElfParser *parser;

    if (!(file = open_mapped_file (filename, error)))
        return NULL;

#if 0
    g_print ("elf parser new : %s\n", filename);
#endif

    data = (guchar *)g_mapped_file_get_contents (file);
    length = g_mapped_file_get_length (file);

#if 0
    g_print ("data %p: for %s\n", data, filename);
#endif

    parser = elf_parser_new_from_data (data, length);

#if 0
    g_print ("Parser for %s: %p\n", filename, parser);
#endif

    if (!parser)
    {
        g_set_error (error,
                     G_FILE_ERROR,
                     G_FILE_ERROR_FAILED,
                     "Failed to load ELF from file %s",
                     filename);
        g_mapped_file_unref (file);
        return NULL;
    }

    parser->filename = g_strdup (filename);

    parser->file = file;

#if 0
    g_print ("Elf file: %s  (debug: %s)\n",
             filename, elf_parser_get_debug_link (parser, NULL));

    if (!parser->symbols)
        g_print ("at this point %s has no symbols\n", filename);
#endif

    return parser;
}

guint32
elf_parser_get_crc32 (ElfParser *parser)
{
    static const unsigned long crc32_table[256] = {
        0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
        0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
        0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
        0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
        0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
        0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
        0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
        0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
        0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
        0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
        0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
        0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
        0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
        0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
        0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
        0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
        0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
        0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
        0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
        0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
        0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
        0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
        0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
        0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
        0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
        0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
        0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
        0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
        0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
        0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
        0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
        0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
        0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
        0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
        0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
        0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
        0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
        0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
        0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
        0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
        0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
        0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
        0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
    };
    const guchar *data;
    gsize length;
    gulong crc;
    gsize i;

    data = parser->data;
    length = parser->length;

    crc = 0xffffffff;

    madvise ((char *)data, length, MADV_SEQUENTIAL);

    for (i = 0; i < length; ++i)
        crc = crc32_table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);

    /* We just read the entire file into memory, but we only really
     * need the symbol table, so swap the whole thing out.
     *
     * We could be more exact here, but it's only a few minor
     * pagefaults.
     */
    if (parser->file)
        madvise ((char *)data, length, MADV_DONTNEED);

    return ~crc & 0xffffffff;
}

void
elf_parser_free (ElfParser *parser)
{
    guint i;

    for (i = 0; i < parser->n_sections; ++i)
        section_free (parser->sections[i]);
    g_free (parser->sections);

    if (parser->file)
        g_mapped_file_unref (parser->file);

    g_free (parser->symbols);

    if (parser->filename)
        g_free (parser->filename);

    if (parser->build_id)
        g_free (parser->build_id);

    g_free (parser);
}

gchar *
elf_demangle (const char *name)
{
    /* Try demangling as rust symbol first as legacy rust symbols can demangle as C++ symbols too
     * but will only get partially demangled in that case.
     */
    gchar *demangled = sysprof_rust_demangle (name, 0);

    if (demangled)
        return demangled;

    demangled = sysprof_cplus_demangle (name);

    if (demangled)
        return demangled;
    else
        return g_strdup (name);
}

/*
 * Looking up symbols
 */
static int
compare_sym (const void *a, const void *b)
{
    const ElfSym *sym_a = a;
    const ElfSym *sym_b = b;

    if (sym_a->address < sym_b->address)
        return -1;
    else if (sym_a->address == sym_b->address)
        return 0;
    else
        return 1;
}

#if 0
static void
dump_symbols (ElfParser *parser, ElfSym *syms, guint n_syms)
{
    int i;

    for (i = 0; i < n_syms; ++i)
    {
        ElfSym *s = &(syms[i]);

        g_print ("   %s: %lx\n", elf_parser_get_sym_name (parser, s), s->address);
    }
}
#endif

static void
read_table (ElfParser *parser,
            const Section *sym_table,
            const Section *str_table)
{
    int sym_size = GET_SIZE (parser, Sym);
    guint i, n_symbols;

#if 0
    g_print ("elf: Reading table for %s\n", parser->filename? parser->filename : "<unknown>");
#endif

    parser->n_symbols = sym_table->size / sym_size;
    parser->symbols = g_new (ElfSym, parser->n_symbols);

#if 0
    g_print ("sym table offset: %d\n", sym_table->offset);
#endif

    n_symbols = 0;
#if 0
    g_print ("n syms: %d\n", parser->n_symbols);
#endif
    for (i = 0; i < parser->n_symbols; ++i)
    {
        guint info;
        gulong addr;
        gulong shndx;

        info = st_info (parser, sym_table->offset, i);
        addr = st_value (parser, sym_table->offset, i);
        shndx = st_shndx (parser, sym_table->offset, i);

#if 0
        g_print ("read symbol: %s (section: %d)\n", get_string_indirct (parser->parser,
                                                                         parser->sym_format, "st_name",
                                                                         str_table->offset),
                 shndx);
#endif

        if (addr != 0                                           &&
            shndx < parser->n_sections                          &&
            parser->sections[shndx] == parser->text_section     &&
            (info & 0xf) == STT_FUNC                            &&
            ((info >> 4) == STB_GLOBAL ||
             (info >> 4) == STB_LOCAL  ||
             (info >> 4) == STB_WEAK))
        {
            parser->symbols[n_symbols].address = addr;
            parser->symbols[n_symbols].table = sym_table->offset;
            parser->symbols[n_symbols].offset = i;

            n_symbols++;

#if 0
            g_print ("    symbol: %s:   %lx\n",
                     get_string_indirect (parser->parser,
                                          parser->sym_format, "st_name",
                                          str_table->offset),
                     addr - parser->text_section->load_address);
            g_print ("        sym %d in %p (info: %d:%d) (func:global  %d:%d)\n",
                     addr, parser, info & 0xf, info >> 4, STT_FUNC, STB_GLOBAL);
#endif
        }
        else if (addr != 0)
        {
#if 0
            g_print ("        rejecting %d in %p (info: %d:%d) (func:global  %d:%d)\n",
                     addr, parser, info & 0xf, info >> 4, STT_FUNC, STB_GLOBAL);
#endif
        }
    }

    parser->sym_strings = str_table->offset;
    parser->n_symbols = n_symbols;

    /* Allocate space for at least one symbol, so that parser->symbols will be
     * non-NULL. If it ends up being NULL, we will be parsing the file over and
     * over.
     */
    parser->symbols = g_renew (ElfSym, parser->symbols, parser->n_symbols + 1);

    qsort (parser->symbols, parser->n_symbols, sizeof (ElfSym), compare_sym);
}

static void
read_symbols (ElfParser *parser)
{
    const Section *symtab = find_section (parser, ".symtab", SHT_SYMTAB);
    const Section *strtab = find_section (parser, ".strtab", SHT_STRTAB);
    const Section *dynsym = find_section (parser, ".dynsym", SHT_DYNSYM);
    const Section *dynstr = find_section (parser, ".dynstr", SHT_STRTAB);

    if (symtab && strtab)
    {
#if 0
        g_print ("reading symbol table of %s\n", parser->filename);
#endif
        read_table (parser, symtab, strtab);
    }
    else if (dynsym && dynstr)
    {
#if 0
        g_print ("reading dynamic symbol table of %s\n", parser->filename);
#endif
        read_table (parser, dynsym, dynstr);
    }
    else
    {
        /* To make sure parser->symbols is non-NULL */
        parser->n_symbols = 0;
        parser->symbols = g_new (ElfSym, 1);
    }
}

static ElfSym *
do_lookup (ElfSym *symbols,
           gulong  address,
           int     first,
           int     last)
{
    if (address >= symbols[last].address)
    {
        return &(symbols[last]);
    }
    else if (last - first < 3)
    {
        while (last >= first)
        {
            if (address >= symbols[last].address)
                return &(symbols[last]);

            last--;
        }

        return NULL;
    }
    else
    {
        int mid = (first + last) / 2;

        if (symbols[mid].address > address)
            return do_lookup (symbols, address, first, mid);
        else
            return do_lookup (symbols, address, mid, last);
    }
}

/* Address should be given in 'offset into text segment' */
const ElfSym *
elf_parser_lookup_symbol (ElfParser *parser,
                          gulong     address)
{
    const ElfSym *result;

    if (!parser->symbols)
    {
#if 0
        g_print ("reading symbols at %p\n", parser);
#endif
        read_symbols (parser);
    }

    if (parser->n_symbols == 0)
        return NULL;

    if (!parser->text_section)
        return NULL;

    address += parser->text_section->load_address;

#if 0
    g_print ("elf: the address we are looking up is %p\n", address);
#endif

    result = do_lookup (parser->symbols, address, 0, parser->n_symbols - 1);

#if 0
    if (result)
    {
        g_print ("  elf: found %s at %lx\n", elf_parser_get_sym_name (parser, result), result->address);
    }
    else
    {
        g_print ("elf: not found\n");
    }
#endif

    if (result)
    {
        gulong size = st_size (parser, result->table, result->offset);

        if (size > 0 && result->address + size <= address)
        {
#if 0
            g_print ("  elf: ends at %lx, so rejecting\n",
                     result->address + size);
#endif
            result = NULL;
        }
    }

    if (result)
    {
        /* Reject the symbols if the address is outside the text section */
        if (address > parser->text_section->load_address + parser->text_section->size)
            result = NULL;
    }

    return result;
}

gulong
elf_parser_get_text_offset (ElfParser *parser)
{
    g_return_val_if_fail (parser != NULL, (gulong)-1);

    if (!parser->text_section)
        return (gulong)-1;

    return parser->text_section->offset;
}

static gchar *
make_hex_string (const guchar *data, int n_bytes)
{
    static const char hex_digits[] = {
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
    };
    GString *string = g_string_new (NULL);
    int i;

    for (i = 0; i < n_bytes; ++i)
    {
        char c = data[i];

        g_string_append_c (string, hex_digits[(c & 0xf0) >> 4]);
        g_string_append_c (string, hex_digits[(c & 0x0f)]);
    }

    return g_string_free (string, FALSE);
}

const gchar *
elf_parser_get_build_id (ElfParser *parser)
{
    if (!parser->checked_build_id)
    {
        const Section *build_id =
            find_section (parser, ".note.gnu.build-id", SHT_NOTE);
        guint64 name_size;
        guint64 desc_size;
        guint64 type;
        const char *name;
        guint64 offset;

        parser->checked_build_id = TRUE;

        if (!build_id)
            return NULL;

        offset = build_id->offset;

        name_size = GET_FIELD (parser, offset, Nhdr, 0, n_namesz);
        desc_size = GET_FIELD (parser, offset, Nhdr, 0, n_descsz);
        type      = GET_FIELD (parser, offset, Nhdr, 0, n_type);

        offset += GET_SIZE (parser, Nhdr);

        name = (char *)(parser->data + offset);

        if (strncmp (name, ELF_NOTE_GNU, name_size) != 0 || type != NT_GNU_BUILD_ID)
            return NULL;

        offset += strlen (name);

        offset = (offset + 3) & (~0x3);

        parser->build_id = make_hex_string (parser->data + offset, desc_size);
    }

    return parser->build_id;
}

const char *
elf_parser_get_debug_link (ElfParser *parser,
                           guint32   *crc32)
{
    guint64 offset;
    const Section *debug_link = find_section (parser, ".gnu_debuglink",
                                              SHT_PROGBITS);
    const gchar *result;

    if (!debug_link)
        return NULL;

    offset = debug_link->offset;

    result = (char *)(parser->data + offset);

    if (crc32)
    {
        int len = strlen (result) + 1;
        offset = (offset + len + 3) & ~0x3;

        *crc32 = GET_UINT32 (parser, offset);
    }

    return result;
}

static const guchar *
get_section (ElfParser *parser,
             const char *name)
{
    const Section *section = find_section (parser, name, SHT_PROGBITS);

    if (section)
        return parser->data + section->offset;
    else
        return NULL;
}

const guchar *
elf_parser_get_eh_frame   (ElfParser   *parser)
{
    return get_section (parser, ".eh_frame");
}

const guchar *
elf_parser_get_debug_frame   (ElfParser   *parser)
{
    return get_section (parser, ".debug_frame");
}

const char *
elf_parser_get_sym_name (ElfParser *parser,
                         const ElfSym *sym)
{
    g_return_val_if_fail (parser != NULL, NULL);

    return (char *)(parser->data + parser->sym_strings +
                    st_name (parser, sym->table, sym->offset));
}

gboolean
elf_parser_owns_symbol (ElfParser *parser,
                        const ElfSym *sym)
{
    ElfSym *first, *last;

    if (!parser->n_symbols)
        return FALSE;

    first = parser->symbols;
    last = parser->symbols + parser->n_symbols - 1;

    return first <= sym && sym <= last;
}

gulong
elf_parser_get_sym_address (ElfParser    *parser,
                            const ElfSym *sym)
{
    return sym->address - parser->text_section->load_address;
}

void
elf_parser_get_sym_address_range (ElfParser    *parser,
                                  const ElfSym *sym,
                                  gulong       *begin,
                                  gulong       *end)
{
    *begin = sym->address - parser->text_section->load_address;
    *end = *begin + st_size (parser, sym->table, sym->offset);
}
