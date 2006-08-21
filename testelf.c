#include <glib.h>
#include "elfparser.h"

const char *n;

static void
check (ElfParser *elf, gulong addr)
{
    const ElfSym *sym = elf_parser_lookup_symbol (elf, addr);

    if (!sym)
    {
	g_print ("not found\n");
	return;
    }
    
    n = elf_sym_get_name (sym);
    
    g_print ("%p  =>    ", (void *)addr);

    if (sym)
    {
	g_print ("found: %s (%p)\n",
		 elf_sym_get_name (sym),
		 (void *)elf_sym_get_address (sym));
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

    int i;

    for (i = 0; i < 5000000; ++i)
    {
	check (elf, 0x077c80f0 - (0x07787000 - 0)); /* gtk_about_dialog_set_artists  (add - (map - offset)) */
	
	check (elf, 0x077c80f0 - (0x07787000 - 0)); /* same (but in the middle of the function */
    }
    return 0;
}

