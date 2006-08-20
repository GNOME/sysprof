#include <glib.h>
#include "elfparser.h"

const char *n;

static void
check (ElfParser *elf, gulong addr)
{
    const ElfSym *sym = elf_parser_lookup_symbol (elf, addr);
    n = elf_sym_get_name (sym);
    
#if 0
    g_print ("%p  =>    ", (void *)addr);
#endif

    if (sym)
    {
#if 0
	g_print ("found: %s (%p)\n",
		 elf_sym_get_name (sym),
		 (void *)elf_sym_get_address (sym));
#endif
    }
    else
    {
#if 0
 	g_print ("not found\n");
#endif
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
	check (elf, 0x3e7ef20); /* gtk_handle_box_end_drag */
	check (elf, 0x3e7ef25); /* same (but in the middle of the function */
    }
    return 0;
}

