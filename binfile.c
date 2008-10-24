/* MemProf -- memory profiler and leak detector
 * Copyright 1999, 2000, 2001, Red Hat, Inc.
 * Copyright 2002, Kristian Rietveld
 *
 * Sysprof -- Sampling, systemwide CPU profiler
 * Copyright 2004, 2005, 2008 Soeren Sandmann
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

/* Most interesting code in this file is lifted from bfdutils.c
 * and process.c from Memprof,
 */
 
#include <glib.h>
#include "binfile.h"
#include <bfd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

static void bfd_nonfatal (const char *string);
static void bfd_fatal (const char *string);

/* Binary File */
struct BinFile
{
    char *	filename;
    int         n_symbols;
    Symbol     *symbols;
    Symbol	undefined;
    int         ref_count;
};

static bfd *
open_bfd (const char *file)
{
    bfd *abfd = bfd_openr (file, NULL);
    
    if (!abfd)
	return NULL;
    
    if (!bfd_check_format (abfd, bfd_object))
    {
	bfd_close (abfd);
	return NULL;
    }
    
    return abfd;
}

static unsigned long
calc_crc32 (unsigned long crc, unsigned char *buf, size_t len)
{
    static const unsigned long crc32_table[256] = {
	0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419,
	0x706af48f, 0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4,
	0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07,
	0x90bf1d91, 0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de,
	0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7, 0x136c9856,
	0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
	0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4,
	0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
	0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3,
	0x45df5c75, 0xdcd60dcf, 0xabd13d59, 0x26d930ac, 0x51de003a,
	0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599,
	0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
	0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190,
	0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f,
	0x9fbfe4a5, 0xe8b8d433, 0x7807c9a2, 0x0f00f934, 0x9609a88e,
	0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
	0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed,
	0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
	0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3,
	0xfbd44c65, 0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2,
	0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a,
	0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5,
	0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa, 0xbe0b1010,
	0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
	0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17,
	0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6,
	0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615,
	0x73dc1683, 0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8,
	0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1, 0xf00f9344,
	0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
	0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a,
	0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
	0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1,
	0xa6bc5767, 0x3fb506dd, 0x48b2364b, 0xd80d2bda, 0xaf0a1b4c,
	0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef,
	0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
	0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe,
	0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31,
	0x2cd99e8b, 0x5bdeae1d, 0x9b64c2b0, 0xec63f226, 0x756aa39c,
	0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
	0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b,
	0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
	0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1,
	0x18b74777, 0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c,
	0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45, 0xa00ae278,
	0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7,
	0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc, 0x40df0b66,
	0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
	0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605,
	0xcdd70693, 0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8,
	0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b,
	0x2d02ef8d
    };
    unsigned char *end;
    
    crc = ~crc & 0xffffffff;
    for (end = buf + len; buf < end; ++buf)
	crc = crc32_table[(crc ^ *buf) & 0xff] ^ (crc >> 8);
    return ~crc & 0xffffffff;;
}

static char *
get_debug_link_info (bfd *abfd, unsigned long *crc32_out)
{
    asection *sect;
    ssize_t debuglink_size;
    unsigned long crc32;
    char *contents;
    int crc_offset;
    
    sect = bfd_get_section_by_name (abfd, ".gnu_debuglink");
    
    if (sect == NULL)
	return NULL;
    
    debuglink_size = bfd_get_section_limit (abfd, sect);

    if (debuglink_size < 6)
    {
	g_warning ("%s: .gnu_debuglink section is %d bytes long",
		   abfd->filename, debuglink_size);
	return NULL;
    }

    contents = g_malloc (debuglink_size);
    bfd_get_section_contents (abfd, sect, contents,
			      (file_ptr)0, (bfd_size_type)debuglink_size);

    /* Sanity check */
    if (!memchr (contents, '\0', debuglink_size - 4))
    {
	g_warning ("%s: Malformed .gnu_debuglink section", abfd->filename);

	g_free (contents);
	return NULL;
    }

    /* Crc value is stored after the filename, aligned up to 4 bytes. */
    crc_offset = strlen (contents) + 1;
    crc_offset = (crc_offset + 3) & ~3;
    
    crc32 = bfd_get_32 (abfd, (bfd_byte *) (contents + crc_offset));
    
    *crc32_out = crc32;
    return contents;
}

static gboolean
separate_debug_file_exists (const char *name, unsigned long crc)
{
    unsigned long file_crc = 0;
    int fd;
    guchar buffer[8*1024];
    int count;
    
    fd = open (name, O_RDONLY);
    if (fd < 0)
	return 0;
    
    while ((count = read (fd, buffer, sizeof (buffer))) > 0)
	file_crc = calc_crc32 (file_crc, buffer, count);
    
    close (fd);

    if (crc != file_crc)
    {
	g_print ("warning: %s has wrong crc\n", name);
	return FALSE;
    }
    else
    {
	return TRUE;
    }
}

/* FIXME - not10: this should probably be detected by config.h -- find out what gdb does*/
static const char *debug_file_directory = "/usr/lib/debug";

static char *
find_separate_debug_file (bfd *abfd)
{
    char *basename;
    char *dir;
    char *debugfile;
    unsigned long crc32;
    
    basename = get_debug_link_info (abfd, &crc32);
    if (basename == NULL)
	return NULL;
    
    dir = g_path_get_dirname (bfd_get_filename (abfd));
    
    /* First try in the same directory as the original file: */
    debugfile = g_build_filename (dir, basename, NULL);
    if (separate_debug_file_exists (debugfile, crc32))
    {
	g_free (basename);
	g_free (dir);
	return debugfile;
    }
    g_free (debugfile);
    
    /* Then try in a subdirectory called .debug */
    debugfile = g_build_filename (dir, ".debug", basename, NULL);
    if (separate_debug_file_exists (debugfile, crc32))
    {
	g_free (basename);
	g_free (dir);
	return debugfile;
    }
    g_free (debugfile);
    
    /* Then try in the global debugfile directory */
    debugfile = g_build_filename (debug_file_directory, dir, basename, NULL);
    if (separate_debug_file_exists (debugfile, crc32))
    {
	g_free (basename);
	g_free (dir);
	return debugfile;
    }
    g_free (debugfile);
    
    g_free (basename);
    g_free (dir);
    return NULL;
}

static asymbol **
slurp_symtab (bfd *abfd, long *symcount)
{
    asymbol **sy = (asymbol **) NULL;
    long storage;
    
    if (!(bfd_get_file_flags (abfd) & HAS_SYMS))
    {
	*symcount = 0;
	return NULL;
    }
    
    storage = bfd_get_symtab_upper_bound (abfd);
    if (storage < 0)
	bfd_fatal (bfd_get_filename (abfd));

    if (storage)
	sy = (asymbol **) malloc (storage);

    *symcount = bfd_canonicalize_symtab (abfd, sy);
    if (*symcount < 0)
	bfd_fatal (bfd_get_filename (abfd));
    
    return sy;
}

extern char *cplus_demangle (const char *mangled, int options);

#define DMGL_PARAMS     (1 << 0)        /* Include function args */
#define DMGL_ANSI       (1 << 1)        /* Include const, volatile, etc */

static char *
demangle (bfd *bfd, const char *name)
{
	char *demangled;
	
	if (bfd_get_symbol_leading_char (bfd) == *name)
		++name;

	demangled = cplus_demangle (name, DMGL_ANSI | DMGL_PARAMS);
	return demangled ? demangled : g_strdup (name);
}

static gint
compare_address (const void *a, const void *b)
{
    const Symbol *symbol1 = a;
    const Symbol *symbol2 = b;

    if (symbol1->address < symbol2->address)
	return -1;
    else if (symbol1->address == symbol2->address)
	return 0;
    else
	return 1;
}

static void
read_symbols (BinFile *bf)
{
    asection *text_section;
    const char *separate_debug_file;
    asymbol **bfd_symbols;
    long n_symbols;
    int i;
    bfd *bfd;
    GArray *symbols;
    file_ptr real_text_offset;

    bf->symbols = NULL;
    bf->n_symbols = 0;
    
    bfd = open_bfd (bf->filename);
    if (!bfd)
	return;

    text_section = bfd_get_section_by_name (bfd, ".text");
    if (!text_section)
    {
	bfd_close (bfd);
	return;
    }

    /* Offset of the text segment in the real binary (not the debug one) */
    real_text_offset = text_section->filepos;
    
    separate_debug_file = find_separate_debug_file (bfd);
    if (separate_debug_file)
    {
	bfd_close (bfd);
	bfd = open_bfd (separate_debug_file);
#if 0
	g_print ("bfd for %s is %s\n", bf->filename, separate_debug_file);
#endif
	if (!bfd)
	    return;
    }
    
    bfd_symbols = slurp_symtab (bfd, &n_symbols);
    
    if (!bfd_symbols)
	return;

    text_section = bfd_get_section_by_name (bfd, ".text");
    if (!text_section)
	return;

    symbols = g_array_new (FALSE, FALSE, sizeof (Symbol));

    /* g_print ("%s: text vma: %x\n", bf->filename, text_section->vma); */

    for (i = 0; i < n_symbols; i++)
    {
	Symbol symbol;
	
	if ((bfd_symbols[i]->flags & BSF_FUNCTION) &&
	    (bfd_symbols[i]->section == text_section))
	{
	    /* Store the address in
	     *
	     *     "offset into text_segment + filepos of text segment in original binary"
	     *
	     * Ie., "file position of original binary"
	     */
	    
	    /* offset into text section  */
	    symbol.address =
		bfd_asymbol_value (bfd_symbols[i]) - text_section->vma + real_text_offset;
	    
	    symbol.name = demangle (bfd, bfd_asymbol_name (bfd_symbols[i]));
#if 0
	    g_print ("computed address for %s: %lx\n", symbol.name, symbol.address);
#endif
	    g_array_append_vals (symbols, &symbol, 1);
	}
    }

    if (n_symbols)
	free (bfd_symbols);

#if 0
    if (!n_symbols)
	g_print ("no symbols found for %s\n", bf->filename);
    else
	g_print ("symbols found for %s\n", bf->filename);
#endif
    
    /* Sort the symbols by address */
    qsort (symbols->data, symbols->len, sizeof(Symbol), compare_address);

    bf->n_symbols = symbols->len;
    bf->symbols = (Symbol *)g_array_free (symbols, FALSE);
} 

static GHashTable *bin_files;

BinFile *
bin_file_new (const char *filename)
{
    BinFile *bf;
    
    if (!bin_files)
	bin_files = g_hash_table_new (g_str_hash, g_str_equal);

    bf = g_hash_table_lookup (bin_files, filename);
    if (bf)
    {
	bf->ref_count++;
    }
    else
    {
	bf = g_new0 (BinFile, 1);
    
	bf->filename = g_strdup (filename);
	
	read_symbols (bf);
	
	bf->undefined.name = g_strdup_printf ("In file %s", filename);
	bf->undefined.address = 0x0;
	bf->ref_count = 1;
	g_hash_table_insert (bin_files, bf->filename, bf);
    }
	
    return bf;
}

void
bin_file_free (BinFile *bf)
{
    if (--bf->ref_count == 0)
    {
	int i;

	g_hash_table_remove (bin_files, bf->filename);
	
	g_free (bf->filename);
	
	for (i = 0; i < bf->n_symbols; ++i)
	    g_free (bf->symbols[i].name);
	g_free (bf->symbols);
	g_free (bf->undefined.name);
	g_free (bf);
    }
}

/**
 * bin_file_lookup_symbol:
 * @bf: A BinFile
 * @address: The address to lookup
 * 
 * Look up a symbol. @address should be in file coordinates
 * 
 **/
const Symbol *
bin_file_lookup_symbol (BinFile    *bf,
			gulong     address)
{
    int first = 0;
    int last = bf->n_symbols - 1;
    int middle = last;
    Symbol *data;
    Symbol *result;
    
    if (!bf->symbols || (bf->n_symbols == 0))
	return &(bf->undefined);
    
    data = bf->symbols;

#if 0
    g_print ("looking up %p in %s ", address, bf->filename);
#endif
    
    if (address < data[last].address)
    {
	/* Invariant: data[first].addr <= val < data[last].addr */
	
	while (first < last - 1)
	{
	    middle = (first + last) / 2;
	    if (address < data[middle].address) 
		last = middle;
	    else
		first = middle;
	}
	/* Size is not included in generic bfd data, so we
	 * ignore it for now. (It is ELF specific)
	 */
	result = &data[first];
    }
    else
    {
	result = &data[last];
    }

#if 0
    g_print ("-> %s\n", result->name);
#endif
    
    /* If the name is "call_gmon_start", the file probably doesn't
     * have any other symbols
     */
    if (strcmp (result->name, "call_gmon_start") == 0)
	return &(bf->undefined);
    else if (strncmp (result->name, "__do_global_ctors_aux", strlen ("__do_global_ctors_aux")) == 0)
    {
#if 0
	g_print ("ctors: %p, pos: %p\n", address, result->address);
#endif
    }
    
    return result;
}


/* Symbol */
Symbol *
symbol_copy  (const Symbol *orig)
{
    Symbol *copy;

    copy = g_new (Symbol, 1);
    copy->name = g_strdup (orig->name);
    copy->address = orig->address;

    return copy;
}

gboolean
symbol_equal (const void *sa,
	      const void *sb)
{
    const Symbol *syma = sa;
    const Symbol *symb = sb;
    
    if (symb->address != syma->address)
	return FALSE;

    /* symbols compare equal if their names are both NULL */
    
    if (!syma->name && !symb->name)
	return TRUE;

    if (!syma)
	return FALSE;

    if (!symb)
	return FALSE;

    return strcmp (syma->name, symb->name) == 0;
}

guint
symbol_hash  (const void *s)
{
    const Symbol *symbol = s;
    
    if (!s)
	return 0;
    
    return symbol->name? g_str_hash (symbol->name) : 0 + symbol->address;
}

void
symbol_free  (Symbol *symbol)
{
    if (symbol->name)
	g_free (symbol->name);
    g_free (symbol);
}

static void
bfd_nonfatal (const char *string)
{
    const char *errmsg = bfd_errmsg (bfd_get_error ());
    
    if (string)
    {
	fprintf (stderr, "%s: %s: %s\n",
		 g_get_application_name(), string, errmsg);
    }
    else
    {
	fprintf (stderr, "%s: %s\n",
		 g_get_application_name(), errmsg);
    }
}

static void
bfd_fatal (const char *string)
{
    bfd_nonfatal (string);
    exit (1);
}
