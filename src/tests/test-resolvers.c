#include <errno.h>
#include <glib.h>
#include <sysprof.h>

static const SysprofCaptureSample *
read_to_next_sample (SysprofCaptureReader *reader)
{
  SysprofCaptureFrameType type;

  while (sysprof_capture_reader_peek_type (reader, &type))
    {
      if (type != SYSPROF_CAPTURE_FRAME_SAMPLE)
        {
          if (!sysprof_capture_reader_skip (reader))
            break;
          continue;
        }

      return sysprof_capture_reader_read_sample (reader);
    }

  return NULL;
}

gint
main (gint  argc,
      gchar *argv[])
{
  g_autoptr(GPtrArray) resolvers = NULL;
  g_autoptr(SysprofCaptureReader) reader = NULL;
  const SysprofCaptureSample *sample;
  const gchar *filename;

  if (argc != 2)
    {
      g_printerr ("usage: %s capture_file\n", argv[0]);
      return 1;
    }

  filename = argv[1];
  resolvers = g_ptr_array_new_with_free_func (g_object_unref);
  g_ptr_array_add (resolvers, sysprof_elf_symbol_resolver_new ());
  g_ptr_array_add (resolvers, sysprof_jitmap_symbol_resolver_new ());
  g_ptr_array_add (resolvers, sysprof_kernel_symbol_resolver_new ());

  if (!(reader = sysprof_capture_reader_new (filename)))
    {
      int errsv = errno;
      g_error ("%s", g_strerror (errsv));
      return 1;
    }

  for (guint r = 0; r < resolvers->len; r++)
    {
      SysprofSymbolResolver *resolver = g_ptr_array_index (resolvers, r);

      sysprof_symbol_resolver_load (resolver, reader);
      sysprof_capture_reader_reset (reader);
    }

  while ((sample = read_to_next_sample (reader)))
    {
      SysprofAddressContext last_context = SYSPROF_ADDRESS_CONTEXT_NONE;

      g_print ("Sample: pid=%d depth=%d\n", sample->frame.pid, sample->n_addrs);

      for (guint a = 0; a < sample->n_addrs; a++)
        {
          SysprofAddress addr = sample->addrs[a];
          SysprofAddressContext context;
          gboolean found = FALSE;

           if (sysprof_address_is_context_switch (addr, &context))
             {
               g_print ("  %02d: %016"G_GINT64_MODIFIER"x: Context Switch\n", a, addr);
               last_context = context;
               continue;
             }

          for (guint r = 0; r < resolvers->len; r++)
            {
              SysprofSymbolResolver *resolver = g_ptr_array_index (resolvers, r);
              g_autofree gchar *symbol = NULL;
              GQuark tag;

              symbol = sysprof_symbol_resolver_resolve_with_context (resolver,
                                                                     sample->frame.time,
                                                                     sample->frame.pid,
                                                                     last_context,
                                                                     addr,
                                                                     &tag);

              if (symbol != NULL)
                {
                  g_print ("  %02d: %016"G_GINT64_MODIFIER"x: %s\n", a, addr, symbol);
                  found = TRUE;
                  break;
                }
            }

          if (!found)
            g_print ("  %02d: %016"G_GINT64_MODIFIER"x: \n", a, addr);
        }
    }

  return 0;
}
