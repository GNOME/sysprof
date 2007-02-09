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
#include <glib.h>

typedef struct ElfSym ElfSym;
typedef struct ElfParser ElfParser;

ElfParser *   elf_parser_new_from_data  (const guchar *data,
					 gsize	       length);
ElfParser *   elf_parser_new            (const char  *filename,
					 GError     **err);
void          elf_parser_free           (ElfParser   *parser);
const char *  elf_parser_get_debug_link (ElfParser   *parser,
					 guint32     *crc32);
const guchar *elf_parser_get_eh_frame   (ElfParser   *parser);
gulong	      elf_parser_get_text_offset (ElfParser  *parser);


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
const char *elf_parser_get_sym_name (ElfParser *parser,
				     const ElfSym *sym);
gulong elf_parser_get_sym_address (ElfParser *parser,
				   const ElfSym *sym);
char *elf_demangle (const char *name);
