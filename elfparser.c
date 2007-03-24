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
#include <elf.h>
#include <sys/mman.h>
#include "binparser.h"
#include "elfparser.h"

typedef struct Section Section;

struct ElfSym
{
    gulong offset;
    gulong address;
};

struct Section
{
    const gchar *	name;
    gsize		offset;
    gsize		size;
    gboolean		allocated;
    gulong		load_address;
    guint		type;
};

struct ElfParser
{
    BinParser *		parser;
    
    BinRecord *		header;
    BinRecord *		strtab_format;
    BinRecord *		shn_entry;
    BinRecord *		sym_format;
    
    int			n_sections;
    Section **		sections;
    
    int			n_symbols;
    ElfSym *		symbols;
    gsize		sym_strings;
    
    GMappedFile *	file;
    
    const Section *	text_section;
};

static gboolean parse_elf_signature (const guchar *data, gsize length,
				     gboolean *is_64, gboolean *is_be);
static void     make_formats        (ElfParser *parser, gboolean is_64);

static const char *
get_string_indirect (BinParser *parser,
		     BinRecord *record,
		     const char *name,
		     gsize str_table)
{
    const char *result = NULL;    
    gsize index;
    
    bin_parser_save (parser);
    
    index = bin_parser_get_uint_field (parser, record, name);
    
    bin_parser_set_offset (parser, str_table + index);
    
    result = bin_parser_get_string (parser);
    
    bin_parser_restore (parser);
    
    return result;
}

static Section *
section_new (BinParser *parser,
	     BinRecord *record,
	     gsize      name_table)
{
    Section *section = g_new (Section, 1);
    guint64 flags;
    
    section->name = get_string_indirect (parser, record, "sh_name", name_table);
    section->size = bin_parser_get_uint_field (parser, record, "sh_size");
    section->offset = bin_parser_get_uint_field (parser, record, "sh_offset");
    
    flags = bin_parser_get_uint_field (parser, record, "sh_flags");
    section->allocated = !!(flags & SHF_ALLOC);
    
    if (section->allocated)
    {
	section->load_address = bin_parser_get_uint_field (
	    parser, record, "sh_addr");
    }
    else
    {
	section->load_address = 0;
    }
    
    section->type = bin_parser_get_uint_field (parser, record, "sh_type");
    
    return section;
}

static void
section_free (Section *section)
{
    g_free (section);
}

static const Section *
find_section (ElfParser *parser,
	      const char *name,
	      guint	  type)
{
    int i;
    
    for (i = 0; i < parser->n_sections; ++i)
    {
	Section *section = parser->sections[i];
	
	if (strcmp (section->name, name) == 0 && section->type == type)
	    return section;
    }
    
    return NULL;
}

ElfParser *
elf_parser_new_from_data (const guchar *data,
			  gsize length)
{
    ElfParser *parser;
    gboolean is_64, is_big_endian;
    int section_names_idx;
    gsize section_names;
    gsize section_headers;
    int i;
    
    if (!parse_elf_signature (data, length, &is_64, &is_big_endian))
    {
	/* FIXME: set error */
	return NULL;
    }
    
    parser = g_new0 (ElfParser, 1);
    
    parser->parser = bin_parser_new (data, length);
    
    if (is_big_endian)
	bin_parser_set_endian (parser->parser, BIN_BIG_ENDIAN);
    else
	bin_parser_set_endian (parser->parser, BIN_LITTLE_ENDIAN);
    
    make_formats (parser, is_64);
    
    
    /* Read ELF header */
    
    bin_parser_set_offset (parser->parser, 0);
    
    parser->n_sections = bin_parser_get_uint_field (
	parser->parser, parser->header, "e_shnum");
    section_names_idx = bin_parser_get_uint_field (
	parser->parser, parser->header, "e_shstrndx");
    section_headers = bin_parser_get_uint_field (
	parser->parser, parser->header, "e_shoff");
    
    /* Read section headers */
    parser->sections = g_new0 (Section *, parser->n_sections);

    bin_parser_set_offset (parser->parser, section_headers);
    
    bin_parser_save (parser->parser);
    
    bin_parser_seek_record (parser->parser, parser->shn_entry,
			    section_names_idx);

    section_names = bin_parser_get_uint_field (
	parser->parser, parser->shn_entry, "sh_offset");

    bin_parser_restore (parser->parser);
    
    for (i = 0; i < parser->n_sections; ++i)
    {
	parser->sections[i] = section_new (parser->parser,
					   parser->shn_entry,
					   section_names);

	bin_parser_seek_record (parser->parser, parser->shn_entry, 1);
    }
    
    /* Cache the text section */
    parser->text_section = find_section (parser, ".text", SHT_PROGBITS);
    if (!parser->text_section)
	parser->text_section = find_section (parser, ".text", SHT_NOBITS);
    
    return parser;
}

ElfParser *
elf_parser_new (const char *filename,
		GError **err)
{
    const guchar *data;
    gsize length;
    ElfParser *parser;

    GMappedFile *file = g_mapped_file_new (filename, FALSE, NULL);
    
    if (!file)
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
    
    if (!parser)
    {
	g_mapped_file_free (file);
	return NULL;
    }
    
    parser->file = file;
    
#if 0
    g_print ("Elf file: %s  (debug: %s)\n",
	     filename, elf_parser_get_debug_link (parser, NULL));
#endif
    
    parser->file = file;
    
#if 0
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
	0xf3b97148, 0x84be41de,	0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
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
    
    data = bin_parser_get_data (parser->parser);
    length = bin_parser_get_length (parser->parser);
    
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
    madvise ((char *)data, length, MADV_DONTNEED);

    return ~crc & 0xffffffff;
}

void
elf_parser_free (ElfParser *parser)
{
    int i;
    
    for (i = 0; i < parser->n_sections; ++i)
	section_free (parser->sections[i]);
    g_free (parser->sections);
    
    if (parser->file)
	g_mapped_file_free (parser->file);
    
    g_free (parser->symbols);
    
    bin_parser_free (parser->parser);
    
    g_free (parser);
}

extern char *sysprof_cplus_demangle (const char *name, int options);

char *
elf_demangle (const char *name)
{
#define DMGL_PARAMS     (1 << 0)        /* Include function args */
#define DMGL_ANSI       (1 << 1)        /* Include const, volatile, etc */
    
    char *demangled = sysprof_cplus_demangle (name, DMGL_PARAMS | DMGL_ANSI);
    
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
    int sym_size = bin_record_get_size (parser->sym_format);
    int i;
    int n_functions;
    
    parser->n_symbols = sym_table->size / sym_size;
    parser->symbols = g_new (ElfSym, parser->n_symbols);
    
#if 0
    g_print ("sym table offset: %d\n", sym_table->offset);
#endif
    
    bin_parser_set_offset (parser->parser, sym_table->offset);
    
    n_functions = 0;
#if 0
    g_print ("n syms: %d\n", parser->n_symbols);
#endif
    for (i = 0; i < parser->n_symbols; ++i)
    {
	guint info;
	gulong addr;
	gulong offset;
	
	info = bin_parser_get_uint_field (
	    parser->parser, parser->sym_format, "st_info");
	addr = bin_parser_get_uint_field (
	    parser->parser, parser->sym_format, "st_value");
	offset = bin_parser_get_offset (parser->parser);
	
	if (addr != 0				&&
	    (info & 0xf) == STT_FUNC		&&
	    ((info >> 4) == STB_GLOBAL ||
	     (info >> 4) == STB_LOCAL))
	{
	    parser->symbols[n_functions].address = addr;
	    parser->symbols[n_functions].offset = offset;
	    
	    n_functions++;
	} 
	
#if 0
	g_print ("read symbol: %s\n", get_string_indirect (parser->parser,
							   parser->sym_format, "st_name",
							   str_table->offset));
#endif
	
	
	bin_parser_seek_record (parser->parser, parser->sym_format, 1);
    }
    
    parser->sym_strings = str_table->offset;
    parser->n_symbols = n_functions;
    parser->symbols = g_renew (ElfSym, parser->symbols, parser->n_symbols);
    
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
	read_table (parser, symtab, strtab);
    }
    else if (dynsym && dynstr)
    {
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
	g_print ("reading symbols\n");
#endif
	read_symbols (parser);
    }
    
    if (parser->n_symbols == 0)
	return NULL;
    
    if (!parser->text_section)
	return NULL;
    
    address += parser->text_section->load_address;
    
#if 0
    g_print ("the address we are looking up is %p\n", address);
#endif
    
    result = do_lookup (parser->symbols, address, 0, parser->n_symbols - 1);
    
#if 0
    if (result)
    {
	g_print ("found %s at %lx\n", elf_parser_get_sym_name (parser, result), result->address);
    }
#endif
    
    if (result)
    {
	gulong size;
	
	bin_parser_set_offset (parser->parser, result->offset);
	
	size = bin_parser_get_uint_field (
	    parser->parser, parser->sym_format, "st_size");
	
	if (result->address + size <= address)
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

const char *
elf_parser_get_debug_link (ElfParser *parser, guint32 *crc32)
{
    const Section *debug_link = find_section (parser, ".gnu_debuglink",
					      SHT_PROGBITS);
    const gchar *result;
    
    if (!debug_link)
	return NULL;
    
    bin_parser_set_offset (parser->parser, debug_link->offset);
    
    result = bin_parser_get_string (parser->parser);
    
    bin_parser_align (parser->parser, 4);
    
    if (crc32)
	*crc32 = bin_parser_get_uint (parser->parser, 4);
    
    return result;
}

const guchar *
elf_parser_get_eh_frame   (ElfParser   *parser)
{
    const Section *eh_frame = find_section (parser, ".eh_frame", SHT_PROGBITS);
    
    if (eh_frame)
	return bin_parser_get_data (parser->parser) + eh_frame->offset;
    else
	return NULL;
}

const char *
elf_parser_get_sym_name (ElfParser *parser,
			 const ElfSym *sym)
{
    const char *result;
    
    g_return_val_if_fail (parser != NULL, NULL);
    
    bin_parser_set_offset (parser->parser, sym->offset);
    result = get_string_indirect (
	parser->parser, parser->sym_format, "st_name", parser->sym_strings);
    
    return result;
}

gulong
elf_parser_get_sym_address (ElfParser *parser,
			    const ElfSym *sym)
{
    return sym->address;
}

/*
 * Utility functions
 */
static gboolean
parse_elf_signature (const guchar *data,
		     gsize	   length,
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

static void
get_formats (gboolean is_64,
	     const BinField **elf_header,
	     const BinField **shn_entry,
	     const BinField **sym_format)
{
    static const BinField elf64_header[] = {
	{ "e_ident",		BIN_UNINTERPRETED, EI_NIDENT },
	{ "e_type",		BIN_UINT,	2 },
	{ "e_machine",		BIN_UINT,	2 }, 
	{ "e_version",		BIN_UINT,	4 },
	{ "e_entry",		BIN_UINT,	8 },
	{ "e_phoff",		BIN_UINT,	8 },
	{ "e_shoff",		BIN_UINT,	8 },
	{ "e_flags",		BIN_UINT,	4 },
	{ "e_ehsize",		BIN_UINT,	2 },
	{ "e_phentsize",	BIN_UINT,	2 },
	{ "e_phnum",		BIN_UINT,	2 },
	{ "e_shentsize",	BIN_UINT,	2 },
	{ "e_shnum",		BIN_UINT,	2 },
	{ "e_shstrndx",		BIN_UINT,	2 },
	{ "" },
    };
    
    static const BinField elf32_header[] = {
	{ "e_ident",		BIN_UNINTERPRETED, EI_NIDENT },
	{ "e_type",		BIN_UINT,	2 },
	{ "e_machine",		BIN_UINT,	2 }, 
	{ "e_version",		BIN_UINT,	4 },
	{ "e_entry",		BIN_UINT,	4 },
	{ "e_phoff",		BIN_UINT,	4 },
	{ "e_shoff",		BIN_UINT,	4 },
	{ "e_flags",		BIN_UINT,	4 },
	{ "e_ehsize",		BIN_UINT,	2 },
	{ "e_phentsize",	BIN_UINT,	2 },
	{ "e_phnum",		BIN_UINT,	2 },
	{ "e_shentsize",	BIN_UINT,	2 },
	{ "e_shnum",		BIN_UINT,	2 },
	{ "e_shstrndx",		BIN_UINT,	2 },
	{ "" },
    };
    
    static const BinField shn64_entry[] = {
	{ "sh_name",		BIN_UINT,	4 },
	{ "sh_type",		BIN_UINT,	4 },
	{ "sh_flags",		BIN_UINT,	8 },
	{ "sh_addr",		BIN_UINT,	8 },
	{ "sh_offset",		BIN_UINT,	8 },
	{ "sh_size",		BIN_UINT,	8 },
	{ "sh_link",		BIN_UINT,	4 },
	{ "sh_info",		BIN_UINT,	4 },
	{ "sh_addralign",	BIN_UINT,	8 },
	{ "sh_entsize",		BIN_UINT,	8 },
	{ "" }
    };
    
    static const BinField shn32_entry[] = {
	{ "sh_name",		BIN_UINT,	4 },
	{ "sh_type",		BIN_UINT,	4 },
	{ "sh_flags",		BIN_UINT,	4 },
	{ "sh_addr",		BIN_UINT,	4 },
	{ "sh_offset",		BIN_UINT,	4 },
	{ "sh_size",		BIN_UINT,	4 },
	{ "sh_link",		BIN_UINT,	4 },
	{ "sh_info",		BIN_UINT,	4 },
	{ "sh_addralign",	BIN_UINT,	4 },
	{ "sh_entsize",		BIN_UINT,	4 },
	{ "" }
    };
    
    static const BinField sym64_format[] = {
	{ "st_name",		BIN_UINT,	4 },
	{ "st_info",		BIN_UINT,	1 },
	{ "st_other",		BIN_UINT,	1 },
	{ "st_shndx",		BIN_UINT,	2 },
	{ "st_value",		BIN_UINT,	8 },
	{ "st_size",		BIN_UINT,	8 },
	{ "" }
    };

    static const BinField sym32_format[] = {
	{ "st_name",		BIN_UINT,	4 },
	{ "st_value",		BIN_UINT,	4 },
	{ "st_size",		BIN_UINT,	4 },
	{ "st_info",		BIN_UINT,	1 },
	{ "st_other",		BIN_UINT,	1 },
	{ "st_shndx",		BIN_UINT,	2 },
	{ "" },
    };

    if (is_64)
    {
	*elf_header = elf64_header;
	*shn_entry  = shn64_entry;
	*sym_format = sym64_format;
    }
    else
    {
	*elf_header = elf32_header;
	*shn_entry  = shn32_entry;
	*sym_format = sym32_format;
    }
}

static void
make_formats (ElfParser *parser, gboolean is_64)
{
    const BinField *elf_header, *shn_entry, *sym_format;

    get_formats (is_64, &elf_header, &shn_entry, &sym_format);

    parser->header = bin_parser_create_record (parser->parser, elf_header);
    parser->shn_entry = bin_parser_create_record (parser->parser, shn_entry);
    parser->sym_format = bin_parser_create_record (parser->parser, sym_format);
}

