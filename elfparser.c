#include <string.h>
#include <elf.h>
#include "binparser.h"
#include "elfparser.h"

typedef struct SymbolTable SymbolTable;

struct SymbolTable
{
};

struct ElfSym
{
};

struct ElfParser
{
    BinParser *parser;
    BinFormat *header;
    BinFormat *strtab_format;
    BinFormat *shn_entry;
    BinFormat *sym_format;
    gsize      strtab_offset;
    gsize      str_table;
};

static void make_formats (ElfParser *parser,
			  gboolean is_64,
			  gboolean is_big_endian);

#if 0
BinFormat *str_tab = bin_field_new_string ("string", make_string());
gsize offset = find_it();
#endif

#if 0
static void
parse_elf (const guchar *data,
	   gsize length)
{
    gboolean is_64, is_big_endian;
    BinFormat *elf_header;
    BinFormat *shn_entry;
    BinParser *parser;
    BinParser *sh_parser;
    BinFormat *sym;
    int i;
    
    find_elf_type (data, length, &is_64, &is_big_endian);
    
    parser = bin_parser_new (data, length);

    bin_parser_begin (parser, elf_header, 0);

    g_print ("section header offset: %u\n",
	     bin_parser_get_uint ("e_shoff"));
    g_print ("There are %llu sections\n",
	     bin_parser_get_uint (parser, "e_shnum"));

}
#endif

static const char *
elf_lookup_string (ElfParser *parser, int offset)
{
    const char *result;

    /* This function has a midleading name. In reality
     * it only looks up in the section header table
     */
    
    bin_parser_begin (parser->parser,
		      NULL, parser->strtab_offset + offset);
    
    result = bin_parser_get_string (parser->parser);
    
    bin_parser_end (parser->parser);

    return result;
}

static gboolean
find_elf_type (const guchar *data, gsize length,
	       gboolean *is_64, gboolean *is_be)
{
    /* FIXME: this function should be able to return an error */
    if (length < EI_NIDENT)
	return FALSE;
    
    /* 32 or 64? */
    if (data[EI_CLASS] == ELFCLASS32)
    {
	*is_64 = FALSE;
    }
    else if (data[EI_CLASS] == ELFCLASS64)
    {
	*is_64 = TRUE;
    }
    else
    {
	/* FIXME: set_error */
	return FALSE;
    }
    
    /* big or little endian? */
    if (data[EI_DATA] == ELFDATA2LSB)
    {
	*is_be = FALSE;
    }
    else if (data[EI_DATA] == ELFDATA2MSB)
    {
	*is_be = TRUE;
    }
    else
    {
	/* FIXME: set error */
	return FALSE;
    }
    
    g_print ("This elf file is %s %s\n",
	     *is_64? "64 bit" : "32 bit",
	     *is_be? "big endiann" : "little endian");
    
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
dump_symbol_table (ElfParser *parser,
		   gsize      offset,
		   gsize      size)
{
    int i;
    if (!parser->str_table)
    {
	g_print ("no string table\n");
	return;
    }

    g_print ("dumping symbol table at %d\n", offset);
    
    bin_parser_begin (parser->parser, parser->sym_format, offset);

    for (i = 0; i < 200; ++i)
    {
	guint64 idx;

	bin_parser_index (parser->parser, i);
	idx = bin_parser_get_uint (parser->parser, "st_name");
	const char *result;
	gsize size;

#if 0
	g_print ("addr: %p\n", bin_parser_get_address (parser->parser, "st_name"));
#endif
#if 0
	g_print ("idx: %d\n", idx);
#endif

	size = bin_parser_get_uint (parser->parser, "st_size");
	
	bin_parser_begin (parser->parser,
			  NULL, parser->str_table + idx);
	
	result = bin_parser_get_string (parser->parser);

	bin_parser_end (parser->parser);

	g_print ("%d symbol: size: %d, %s.\n",
		 i, size, result);
    }
    
    bin_parser_end (parser->parser);
}

ElfParser *
elf_parser_new (const guchar *data, gsize length)
{
    ElfParser *parser;
    gboolean is_64, is_big_endian;
    int n_sections;
    int section_name_table;
    int i;
    
    if (!find_elf_type (data, length, &is_64, &is_big_endian))
    {
	/* FIXME: set error */
	return NULL;
    }
    
    parser = g_new0 (ElfParser, 1);
    
    parser->parser = bin_parser_new (data, length);

    make_formats (parser, is_64, is_big_endian);
    
    bin_parser_begin (parser->parser, parser->header, 0);

    n_sections =
	bin_parser_get_uint (parser->parser, "e_shnum");
    
    section_name_table =
	bin_parser_get_uint (parser->parser, "e_shstrndx");

    bin_parser_begin (
	parser->parser, parser->shn_entry,
	bin_parser_get_uint (parser->parser, "e_shoff"));
    
    bin_parser_index (parser->parser, section_name_table);

    parser->strtab_offset =
	bin_parser_get_uint (parser->parser, "sh_offset");

    for (i = 0; i < n_sections; ++i)
    {
	const char *name;
	int offset;
	
	bin_parser_index (parser->parser, i);
	offset = bin_parser_get_uint (parser->parser, "sh_name");
	name = elf_lookup_string (parser, offset);

	if (strcmp (name, ".strtab") == 0)
	{
	    parser->str_table = bin_parser_get_uint (
		parser->parser, "sh_offset");
	}
    }
    
    for (i = 0; i < n_sections; ++i)
    {
	const char *name;
	int offset;
	const char *type;
	
	bin_parser_index (parser->parser, i);

	offset = bin_parser_get_uint (parser->parser, "sh_name");

	name = elf_lookup_string (parser, offset);

	switch (bin_parser_get_uint (parser->parser, "sh_type"))
	{
	case SHT_NULL:
	    type = "undefined";
	    break;
	case SHT_PROGBITS:
	    type = "progbits";
	    break;
	case SHT_SYMTAB:
	    type = "symbol table";
	    dump_symbol_table (
		parser,
		bin_parser_get_uint (parser->parser, "sh_offset"),
		bin_parser_get_uint (parser->parser, "sh_size"));
	    break;
	case SHT_STRTAB:
	    type = "string table";
	    break;
	case SHT_RELA:
	    type = "relocations with explicit addends";
	    break;
	case SHT_HASH:
	    type = "symbol hash table";
	    break;
	case SHT_DYNAMIC:
	    type = "Information for dynamic linking";
	    break;
	case SHT_NOTE:
	    type = "note";
	    break;
	case SHT_NOBITS:
	    type = "nobits";
	    break;
	case SHT_REL:
	    type = "relocations without explicit addends";
	    break;
	case SHT_SHLIB:
	    type = "reserved with unspecified semantics";
	    break;
	case SHT_DYNSYM:
	    type = "dynamic symbols";
	    break;
	case SHT_LOPROC:
	    type = "loproc";
	    break;
	case SHT_HIPROC:
	    type = "hiproc";
	    break;
	case SHT_LOUSER:
	    type = "louser:";
	    break;
	case SHT_HIUSER:
	    type = "hiuser";
	    break;
	default:
	    type = "<unknown>";
	    break;
	}

	g_print ("%s [%s] (%d)\n", name, type, offset);
    }
    
    bin_parser_end (parser->parser);
    bin_parser_end (parser->parser);
    
    return parser;
}

static const char *
get_string (BinParser *parser,
	    gsize      table,
	    gsize      offset)
{
    const char *result = NULL;
    
    bin_parser_begin (parser, NULL, table + offset);

    result = bin_parser_get_string (parser);
    
    bin_parser_end (parser);

    return result;
}

static gssize
find_section (ElfParser  *parser,
	      const char *name)
{
    int n_sections;
    int section_name_table;
    int section_headers_offset;
    int section_name_table_offset;
    BinParser *bparser = parser->parser;
    int i;
    gssize result;
    
    bin_parser_begin (parser->parser, parser->header, 0);

    n_sections		      = bin_parser_get_uint (bparser, "e_shnum");
    section_name_table	      = bin_parser_get_uint (bparser, "e_shstrndx");
    section_headers_offset    = bin_parser_get_uint (bparser, "e_shoff");

    bin_parser_begin (bparser, parser->shn_entry, section_headers_offset);

    bin_parser_index (bparser, section_name_table);
    section_name_table_offset = bin_parser_get_uint (bparser, "sh_offset");

    result = -1;
    
    for (i = 0; i < n_sections; ++i)
    {
	const char *section_name;
	gsize name_offset;

	bin_parser_index (bparser, i);

	name_offset = bin_parser_get_uint (bparser, "sh_name");
	
	section_name = get_string (
	    bparser, section_name_table_offset, name_offset);

	if (strcmp (section_name, name) == 0)
	{
	    result = bin_parser_get_uint (bparser, "sh_offset");
	    goto out;
	}
    }

out:
    bin_parser_end (bparser);

    g_print ("found %s at %d\n", name, result);
    
    return result;
}

const ElfSym *
elf_parser_lookup_symbol (ElfParser *parser,
			  gulong     address)
{
    gssize symtab_offset = find_section (parser, ".symtab");
    gssize strtab_offset = find_section (parser, ".strtab");
    gssize dynsym_offset = find_section (parser, ".dynsym");
    gssize dynstr_offset = find_section (parser, ".dynstr");

    return NULL;
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

