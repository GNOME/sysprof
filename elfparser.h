typedef struct ElfSym ElfSym;
typedef struct ElfParser ElfParser;

ElfParser *elf_parser_new (const guchar *data,
			   gsize length);

const ElfSym *elf_parser_lookup_symbol (ElfParser *parser,
					gulong     address);
