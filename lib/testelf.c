#include <glib.h>
#include "elfparser.h"
#include <string.h>

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
    ElfParser *debug = NULL;
    const char *build_id;
    const char *filename;
    const char *dir;

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

    dir = g_path_get_dirname (filename);

    build_id = elf_parser_get_build_id (elf);
    
    guint crc = elf_parser_get_crc32 (elf);

    g_print ("build ID: %s crc: %x\n", build_id, crc);
    filename = elf_parser_get_debug_link(elf, &crc);
    if (filename)
    {
	filename = g_build_filename ("/usr", "lib", "debug", dir, filename, NULL);
	g_print ("Debug link: %s crc: %x\n", filename, crc);
	debug = elf_parser_new (filename, NULL);

	if (debug)
	{
	    const char *build = elf_parser_get_build_id (debug);
	    guint crc_debug = elf_parser_get_crc32 (debug);
	    g_print ("Debug link build ID: %s crc: %x\n", build, crc_debug);
	    if (strcmp(build, build_id) != 0 || crc_debug != crc)
		g_print ("Build ID or crc not matching!\n");
	}
	else
	{
	    g_print ("Separate debug symbol file not found\n");
	}

    }
    else
    {
	g_print ("No debug link\n");
    }
    
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

