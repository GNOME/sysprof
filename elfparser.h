#include <glib.h>

typedef struct ElfSym ElfSym;
typedef struct ElfParser ElfParser;

ElfParser *elf_parser_new (const guchar *data,
			   gsize length);
ElfParser *elf_parser_new_from_file (const char *filename,
				     GError **err);
void       elf_parser_free (ElfParser *parser);

/* Lookup a symbol in the file.
 *
 * The symbol returned is const, so don't free it it or anything. It
 * will be valid until elf_parser_free() is called on the parser.
 *
 *
 * The address should be given in "file coordinates". This means that
 * if the file is mapped at address m and offset o, then an address a
 * should be looked up as  "a - (m - o)".  (m - o) is where the start
 * of the file would have been mapped, so a - (m - o) is the position
 * in the file of a.
 */
const ElfSym *elf_parser_lookup_symbol (ElfParser *parser,
					gulong     address);
guint32 elf_parser_get_crc32 (ElfParser *parser);
const char *elf_sym_get_name (const ElfSym *sym);
gulong elf_sym_get_address (const ElfSym *sym);
char *elf_demangle (const char *name);
