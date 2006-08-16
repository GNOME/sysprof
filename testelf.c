#include <glib.h>
#include "elfparser.h"

int
main ()
{
    int i;
    
    GMappedFile *libgtk = g_mapped_file_new ("/usr/lib/libgtk-x11-2.0.so",
					     FALSE, NULL);
    
    
#if 0
    for (i = 0; i < 50000; ++i)
#endif
    {
	ElfParser *elf = elf_parser_new (
	    g_mapped_file_get_contents (libgtk),
	    g_mapped_file_get_length (libgtk));
	
	elf_parser_lookup_symbol (elf, 1000);
    }
}

