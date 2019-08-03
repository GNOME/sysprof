#include "sysprof-flatpak.h"

gint
main (gint   argc,
      gchar *argv[])
{
  g_auto(GStrv) debug_dirs = sysprof_flatpak_debug_dirs ();

  for (guint i = 0; debug_dirs[i]; i++)
    g_print ("%s\n", debug_dirs[i]);

  return 0;
}
