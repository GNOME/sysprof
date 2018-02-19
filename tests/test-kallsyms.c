#include "../lib/sp-address.h"
#include "../lib/sp-kallsyms.h"

#include <glib.h>
#include <stdlib.h>

int
main (gint argc,
      gchar *argv[])
{
  g_autoptr(SpKallsyms) kallsyms = NULL;
  const gchar *name;
  SpAddress addr = 0;
  guint8 type;

  if (argc < 2)
    {
      g_printerr ("usage: %s FILE\n", argv[0]);
      return EXIT_FAILURE;
    }

  kallsyms = sp_kallsyms_new (argv[1]);

  while (sp_kallsyms_next (kallsyms, &name, &addr, &type))
    {
      g_assert (name != NULL);
      g_assert (addr != 0);
      g_assert (type != 0);

      g_print ("%s %"G_GUINT64_FORMAT"x\n", name, addr);
    }

  return EXIT_SUCCESS;
}
