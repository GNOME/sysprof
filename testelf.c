#include <glib.h>
#include "elfparser.h"

static void
check (ElfParser *elf, gulong addr)
{
    const ElfSym *sym = elf_parser_lookup_symbol (elf, addr);
    
    g_print ("%p  =>    ", addr);

    if (sym)
    {
	g_print ("found: %s (%p)\n",
		 elf_sym_get_name (sym),
		 elf_sym_get_address (sym));
    }
    else
    {
	g_print ("not found\n");
    }
}

int
main ()
{
    GMappedFile *libgtk = g_mapped_file_new ("/usr/lib/libgtk-x11-2.0.so",
					     FALSE, NULL);
    ElfParser *elf = elf_parser_new (
	g_mapped_file_get_contents (libgtk),
	g_mapped_file_get_length (libgtk));
    
    check (elf, 0x3e7ef20); /* gtk_handle_box_end_drag */
    check (elf, 0x3e7ef25); /* same */

    return 0;
}

