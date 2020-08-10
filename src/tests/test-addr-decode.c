#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <sysprof.h>

#include "sysprof-platform.h"
#include "sysprof-capture-symbol-resolver.h"

gint
main (gint argc,
      gchar *argv[])
{
  g_autoptr(SysprofCaptureReader) reader = NULL;
  g_autoptr(SysprofSymbolResolver) resolver = NULL;
  SysprofCaptureFrameType type;

  if (argc != 2)
    {
      g_printerr ("usage: %s CAPTURE_FILE\n", argv[0]);
      return 1;
    }

  if (!(reader = sysprof_capture_reader_new (argv[1])))
    {
      int errsv = errno;
      g_printerr ("%s\n", g_strerror (errsv));
      return 1;
    }

  resolver = sysprof_capture_symbol_resolver_new ();
  sysprof_symbol_resolver_load (resolver, reader);

  sysprof_capture_reader_reset (reader);

  while (sysprof_capture_reader_peek_type (reader, &type))
    {
      if (type == SYSPROF_CAPTURE_FRAME_SAMPLE)
        {
          const SysprofCaptureSample *sample;
          SysprofAddressContext last_context = SYSPROF_ADDRESS_CONTEXT_NONE;

          if ((sample = sysprof_capture_reader_read_sample (reader)))
            {
              for (guint i = 0; i < sample->n_addrs; i++)
                {
                  g_autofree gchar *name = NULL;
                  SysprofAddressContext context;
                  GQuark tag = 0;

                  if (sysprof_address_is_context_switch (sample->addrs[i], &context))
                    {
                      last_context = context;
                      continue;
                    }

                  name = sysprof_symbol_resolver_resolve_with_context (resolver,
                                                                       sample->frame.time,
                                                                       sample->frame.pid,
                                                                       last_context,
                                                                       sample->addrs[i],
                                                                       &tag);

                  g_print ("%u: %s [%s]\n",
                           i,
                           name ? name : "-- missing --",
                           tag ? g_quark_to_string (tag) : "No Tag");
                }

              g_print ("======\n");

              continue;
            }
        }

      if (!sysprof_capture_reader_skip (reader))
        break;
    }

  return 0;
}
