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
    
    n = elf_parser_get_sym_name (elf, sym);
    
    g_print ("%p  =>    ", (void *)addr);

    if (sym)
    {
	g_print ("found: %s (%p)\n",
		 elf_parser_get_sym_name (elf, sym),
		 (void *)elf_parser_get_sym_address (elf, sym));
    }
    else
    {
 	g_print ("not found\n");
    }
}

int
main (int argc, char **argv)
{
    ElfParser *elf;
    const char *build_id;
    const char *filename;

    if (argc == 1)
	filename = "/usr/lib/libgtk-x11-2.0.so";
    else
	filename = argv[1];

    elf = elf_parser_new (filename, NULL);

    if (!elf)
    {
	g_print ("NO ELF!!!!\n");
	return -1;
    }

    build_id = elf_parser_get_build_id (elf);

    g_print ("build ID: %s\n", build_id);
    
    elf_parser_get_crc32 (elf);
    
#if 0
    for (i = 0; i < 5000000; ++i)
#endif
    {
	elf_parser_get_crc32 (elf);
	check (elf, 0x077c80f0 - (0x07787000 - 0)); /* gtk_about_dialog_set_artists  (add - (map - offset)) */
	
	check (elf, 0x077c80f0 - (0x07787000 - 0)); /* same (but in the middle of the function */
    }
    return 0;
}

