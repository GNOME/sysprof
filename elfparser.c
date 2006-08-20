#include <stdlib.h>
#include <string.h>
#include <elf.h>
#include "binparser.h"
#include "elfparser.h"

typedef struct SymbolTable SymbolTable;
typedef struct Section Section;

struct SymbolTable
{
};

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
};

struct ElfParser
{
    BinParser *	parser;
    
    BinFormat *	header;
    BinFormat *	strtab_format;
    BinFormat *	shn_entry;
    BinFormat *	sym_format;
    
    int		n_sections;
    Section **	sections;

    int		n_symbols;
    ElfSym *	symbols;
    gsize	sym_strings;
};

static gboolean parse_elf_signature (const guchar *data, gsize length,
				     gboolean *is_64, gboolean *is_be);
static void     make_formats        (ElfParser *parser,
				     gboolean is_64,
				     gboolean is_big_endian);

static const char *
get_string (BinParser *parser,
	    gsize      table,
	    const char *name)
{
    const char *result = NULL;
    gsize index;
    
    index = bin_parser_get_uint (parser, name);
    
    bin_parser_begin (parser, NULL, table + index);
    
    result = bin_parser_get_string (parser);
    
    bin_parser_end (parser);
    
    return result;
}

static Section *
section_new (ElfParser *parser,
	     gsize      name_table)
{
    BinParser *bparser = parser->parser;
    Section *section = g_new (Section, 1);
    
    section->name = get_string (bparser, name_table, "sh_name");
    section->size = bin_parser_get_uint (bparser, "sh_size");
    section->offset = bin_parser_get_uint (bparser, "sh_offset");

    return section;
}

static void
section_free (Section *section)
{
    g_free (section);
}

static const Section *
find_section (ElfParser *parser,
	      const char *name)
{
    int i;
    
    for (i = 0; i < parser->n_sections; ++i)
    {
	Section *section = parser->sections[i];
	
	if (strcmp (section->name, name) == 0)
	    return section;
    }
    
    return NULL;
}

static GList *all_elf_parsers = NULL;

ElfParser *
elf_parser_new (const guchar *data, gsize length)
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
    
    make_formats (parser, is_64, is_big_endian);
    
    /* Read ELF header */
    bin_parser_begin (parser->parser, parser->header, 0);
    
    parser->n_sections = bin_parser_get_uint (parser->parser, "e_shnum");
    section_names_idx = bin_parser_get_uint (parser->parser, "e_shstrndx");
    section_headers = bin_parser_get_uint (parser->parser, "e_shoff");
    
    bin_parser_end (parser->parser);
    
    /* Read section headers */
    parser->sections = g_new0 (Section *, parser->n_sections);
    
    bin_parser_begin (parser->parser, parser->shn_entry, section_headers);

    bin_parser_index (parser->parser, section_names_idx);
    section_names = bin_parser_get_uint (parser->parser, "sh_offset");
    
    for (i = 0; i < parser->n_sections; ++i)
    {
	bin_parser_index (parser->parser, i);
	
	parser->sections[i] = section_new (parser, section_names);
    }
    
    bin_parser_end (parser->parser);

    all_elf_parsers = g_list_prepend (all_elf_parsers, parser);
    
    return parser;
}

void
elf_parser_free (ElfParser *parser)
{
    int i;

    all_elf_parsers = g_list_remove (all_elf_parsers, parser);
    
    for (i = 0; i < parser->n_sections; ++i)
	section_free (parser->sections[i]);
    g_free (parser->sections);
    
    g_free (parser);
}

extern char *sysprof_cplus_demangle (const char *name, int options);

char *
elf_demangle (const char *name)
{
    char *demangled = sysprof_cplus_demangle (name, 0);

    if (demangled)
	return demangled;
    else
	return g_strdup (name);
}

/*
 * Looking up symbols
 */
#if 0
#define ELF32_ST_BIND(val)		(((unsigned char) (val)) >> 4)
#define ELF32_ST_TYPE(val)		((val) & 0xf)
#define ELF32_ST_INFO(bind, type)	(((bind) << 4) + ((type) & 0xf))

/* Both Elf32_Sym and Elf64_Sym use the same one-byte st_info field.  */
#define ELF64_ST_BIND(val)		ELF32_ST_BIND (val)
#define ELF64_ST_TYPE(val)		ELF32_ST_TYPE (val)
#define ELF64_ST_INFO(bind, type)	ELF32_ST_INFO ((bind), (type))
#endif

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

static void
read_table (ElfParser *parser,
	    const Section *sym_table,
	    const Section *str_table)
{
    int sym_size = bin_format_get_size (parser->sym_format);
    int i;
    int n_functions;

    parser->n_symbols = sym_table->size / sym_size;
    parser->symbols = g_new (ElfSym, parser->n_symbols);
    
    g_print ("\nreading %d symbols (@%d bytes) from %s\n",
	     parser->n_symbols, sym_size, sym_table->name);
    
    bin_parser_begin (parser->parser, parser->sym_format, sym_table->offset);

    n_functions = 0;
    for (i = 0; i < parser->n_symbols; ++i)
    {
	guint info;
	gulong addr;
	const char *name;
	gulong offset;
	
	bin_parser_index (parser->parser, i);
	
	info = bin_parser_get_uint (parser->parser, "st_info");
	addr = bin_parser_get_uint (parser->parser, "st_value");
	name = get_string (parser->parser, str_table->offset, "st_name");
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
    }
    
    bin_parser_end (parser->parser);

    g_print ("found %d functions\n", n_functions);

    parser->sym_strings = str_table->offset;
    parser->n_symbols = n_functions;
    parser->symbols = g_renew (ElfSym, parser->symbols, parser->n_symbols);
    
    qsort (parser->symbols, parser->n_symbols, sizeof (ElfSym), compare_sym);
}

static void
read_symbols (ElfParser *parser)
{
    const Section *symtab = find_section (parser, ".symtab");
    const Section *strtab = find_section (parser, ".strtab");
    const Section *dynsym = find_section (parser, ".dynsym");
    const Section *dynstr = find_section (parser, ".dynstr");

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
do_lookup (ElfSym *symbols, gulong address, int first, int last)
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
	{
	    return do_lookup (symbols, address, first, mid);
	}
	else
	{
	    return do_lookup (symbols, address, mid, last);
	}
    }
}

const ElfSym *
elf_parser_lookup_symbol (ElfParser *parser,
			  gulong     address)
{
    if (!parser->symbols)
	read_symbols (parser);

    if (parser->n_symbols == 0)
	return NULL;

    /* FIXME: we should offset address based on the files load address */
    
    return do_lookup (parser->symbols, address, 0, parser->n_symbols - 1);
}

static ElfParser *
parser_from_sym (const ElfSym *sym)
{
    GList *list;
    
    /* FIXME: This is  gross, but the alternatives I can think of
     * are all worse.
     */
    for (list = all_elf_parsers; list != NULL; list = list->next)
    {
	ElfParser *parser = list->data;

	if (sym >= parser->symbols &&
	    sym < parser->symbols + parser->n_symbols)
	{
	    return parser;
	}
    }

    return NULL;
}

const char *
elf_sym_get_name (const ElfSym *sym)
{
    ElfParser *parser = parser_from_sym (sym);
    const char *result;

    g_return_val_if_fail (parser != NULL, NULL);

    bin_parser_begin (parser->parser, parser->sym_format, sym->offset);

    result = get_string (parser->parser, parser->sym_strings, "st_name");
    
    bin_parser_end (parser->parser);
    
    return result;
}

gulong
elf_sym_get_address (const ElfSym *sym)
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
	*is_64 = (EI_CLASS == ELFCLASS64);
    
    if (is_be)
	*is_be = (EI_DATA == ELFDATA2MSB);
    
    return TRUE;
}

static BinField *
make_word (gboolean is_64)
{
    if (is_64)
	return bin_field_new_uint64 ();
    else
	return bin_field_new_uint32 ();
}

static void
make_formats (ElfParser *parser, gboolean is_64, gboolean is_big_endian)
{
    parser->header = bin_format_new (
	is_big_endian,
	"e_ident",	bin_field_new_fixed_array (EI_NIDENT, 1),
	"e_type",	bin_field_new_uint16 (),
	"e_machine",	bin_field_new_uint16 (),
	"e_version",	bin_field_new_uint32 (),
	"e_entry",	make_word (is_64),
	"e_phoff",	make_word (is_64),
	"e_shoff",	make_word (is_64),
	"e_flags",	bin_field_new_uint32 (),
	"e_ehsize",	bin_field_new_uint16 (),
	"e_phentsize",	bin_field_new_uint16 (),
	"e_phnum",	bin_field_new_uint16 (),
	"e_shentsize",	bin_field_new_uint16 (),
	"e_shnum",	bin_field_new_uint16 (),
	"e_shstrndx",	bin_field_new_uint16 (),
	NULL);
    
    parser->shn_entry = bin_format_new (
	is_big_endian,
	"sh_name",	bin_field_new_uint32 (),
	"sh_type",	bin_field_new_uint32 (),
	"sh_flags",	make_word (is_64),
	"sh_addr",	make_word (is_64),
	"sh_offset",	make_word (is_64),
	"sh_size",	make_word (is_64),
	"sh_link",	bin_field_new_uint32 (),
	"sh_info",	bin_field_new_uint32 (),
	"sh_addralign",	make_word (is_64),
	"sh_entsize",	make_word (is_64),
	NULL);
    
    if (is_64)
    {
	parser->sym_format = bin_format_new (
	    is_big_endian,
	    "st_name",		bin_field_new_uint32 (),
	    "st_info",		bin_field_new_uint8 (),
	    "st_other",		bin_field_new_uint8 (),
	    "st_shndx",		bin_field_new_uint16 (),
	    "st_value",		bin_field_new_uint64 (),
	    "st_size",		bin_field_new_uint64 (),
	    NULL);
    }
    else
    {
	parser->sym_format = bin_format_new (
	    is_big_endian,
	    "st_name",		bin_field_new_uint32 (),
	    "st_value",		bin_field_new_uint32 (),
	    "st_size",		bin_field_new_uint32 (),
	    "st_info",		bin_field_new_uint8 (),
	    "st_other",		bin_field_new_uint8 (),
	    "st_shndx",		bin_field_new_uint16 (),
	    NULL);
    }
}

