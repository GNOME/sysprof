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
    ElfParser *elf;
    int i;

    elf = elf_parser_new_from_file ("/usr/lib/libgtk-x11-2.0.so", NULL);

    if (!elf)
    {
	g_print ("NO ELF!!!!\n");
	return -1;
    }

    elf_parser_get_crc32 (elf);
    
    for (i = 0; i < 5000000; ++i)
    {
	elf_parser_get_crc32 (elf);
	check (elf, 0x077c80f0 - (0x07787000 - 0)); /* gtk_about_dialog_set_artists  (add - (map - offset)) */
	
	check (elf, 0x077c80f0 - (0x07787000 - 0)); /* same (but in the middle of the function */
    }
    return 0;
}

