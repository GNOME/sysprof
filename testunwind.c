#include "elfparser.h"
#include "unwind.h"

int
main (int argc, char **argv)
{
    const guchar *data;
    ElfParser *elf;

    if (argc == 1)
    {
	g_print ("no arg\n");
	return -1;
    }

    elf = elf_parser_new (argv[1], NULL);

    if (!elf)
    {
	g_print ("NO ELF!!!!\n");
	return -1;
    }

    unwind (elf);
    
    return 0;
}
