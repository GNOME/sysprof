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
    
    return parser;
}

void
elf_parser_free (ElfParser *parser)
{
    int i;
    
    for (i = 0; i < parser->n_sections; ++i)
	section_free (parser->sections[i]);
    g_free (parser->sections);
    
    g_free (parser);
}

/*
 * Looking up symbols
 */
#if 0
static int lookup_function_symbol (ElfParser *parser,
				   int begin,
				   int end,
				   gulong address);

static gboolean
check_symbol (ElfParser *parser,
	      int	 index,
	      gulong	 address)
{
    bin_parser_index (parser->parser, index);
    
    /* FIXME */
    
    return FALSE;
}

static gboolean
is_function (ElfParser *parser, int index)
{
    return FALSE;
}

static gulong
get_address (ElfParser *parser, int index)
{
    return 0;
}

static int
do_check (ElfParser *parser,
	  int begin,
	  int current,
	  int other,
	  int end,
	  gulong address)
{
    int first = current > other ? current : other;
    int last  = current > other ? other : current;
    
    /* The invariant here is that nothing between first
     * and last is a function
     */
    
    if (is_function (parser, current))
    {
	gulong addr = get_address (parser, current);
	
	if (addr == address)
	{
	    return current;
	}
	else if (addr > address)
	{
	    return lookup_function_symbol (
		parser, begin, first, address);
	}
	else
	{
	    return lookup_function_symbol (
		parser, last, end, address);
	}
    }
    
    return -1;
}

static int
lookup_function_symbol (ElfParser *parser,
			int	   begin,
			int	   end,
			gulong	   address)
{
    g_assert (end - begin > 0);
    
    if (end - begin < 10)
    {
	int i;
	
	for (i = 0; i < end - begin; ++i)
	{
	    bin_parser_index (parser->parser, i);
	    
	    if (is_function (parser, i)		&&
		get_address (parser, i == address))
	    {
		return i;
	    }
	}
    }
    else
    {
	int mid1, mid2;
	
	mid1 = mid2 = (end - begin) / 2;
	
	while (mid1 >= begin &&
	       mid2 < end)
	{
	    int res;
	    
	    res = do_check (parser, begin, mid1, mid2, end, address);
	    if (res > 0)
		return res;
	    
	    res = do_check (parser, begin, mid2, mid1, end, address);
	    if (res > 0)
		return res;
	    
	    mid1--;
	    mid2++;
	}
    }
    
    return -1;
}
#endif

#if 0
#define ELF32_ST_BIND(val)		(((unsigned char) (val)) >> 4)
#define ELF32_ST_TYPE(val)		((val) & 0xf)
#define ELF32_ST_INFO(bind, type)	(((bind) << 4) + ((type) & 0xf))

/* Both Elf32_Sym and Elf64_Sym use the same one-byte st_info field.  */
#define ELF64_ST_BIND(val)		ELF32_ST_BIND (val)
#define ELF64_ST_TYPE(val)		ELF32_ST_TYPE (val)
#define ELF64_ST_INFO(bind, type)	ELF32_ST_INFO ((bind), (type))
#endif

static ElfSym *
lookup_symbol (ElfParser *parser,
	       const Section *sym_table,
	       const Section *str_table,
	       gulong address)
{
    int n_symbols = sym_table->size / bin_format_get_size (parser->sym_format);
    int i;

    g_print ("\ndumping %d symbols from %s\n", n_symbols, sym_table->name);
    
    bin_parser_begin (parser->parser, parser->sym_format, sym_table->offset);

    for (i = 0; i < n_symbols; ++i)
    {
	const char *name;
	gulong addr;
	guint info;

	bin_parser_index (parser->parser, i);
	name = get_string (parser->parser, str_table->offset, "st_name");
	info = bin_parser_get_uint (parser->parser, "st_info");
	addr = bin_parser_get_uint (parser->parser, "st_value");

	if ((info & 0xf) == STT_FUNC)
	    g_print ("symbol: %8lx, %s\n", addr, name);
    }
    
    bin_parser_end (parser->parser);

    return NULL;
}

const ElfSym *
elf_parser_lookup_symbol (ElfParser *parser,
			  gulong     address)
{
    const Section *symtab = find_section (parser, ".symtab");
    const Section *dynsym = find_section (parser, ".dynsym");
    const Section *strtab = find_section (parser, ".strtab");
    const Section *dynstr = find_section (parser, ".dynstr");

    if (strtab && symtab)
    {
	lookup_symbol (parser, symtab, strtab, address);
    }

    if (dynsym && dynstr)
    {
	lookup_symbol (parser, dynsym, dynstr, address);
    }
    
    g_print ("HELLO!!\n");
    
    return NULL;
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

