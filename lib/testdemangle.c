#include "elfparser.h"

int
main ()
{
    printf ("%s\n", elf_demangle ("_ZN8Inkscape7FiltersL12filter2D_FIRIhLj4EEEvPT_iiPKS2_iiiiPKNS_4Util10FixedPointIjLj16EEEii"));
}
