#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <sysprof.h>

#include "sysprof-platform.h"
#include "sysprof-symbol-map.h"

static GMainLoop *main_loop;

static void *
resolve_in_thread (gpointer data)
{
  SysprofCaptureReader *reader = data;
  g_autoptr(SysprofSymbolResolver) kernel = NULL;
  g_autoptr(SysprofSymbolResolver) elf = NULL;
  SysprofCaptureFrameType type;
  SysprofSymbolMap *map;
  gboolean r;
  int fd;

  g_assert (reader != NULL);

  map = sysprof_symbol_map_new ();
  kernel = sysprof_kernel_symbol_resolver_new ();
  elf = sysprof_elf_symbol_resolver_new ();

  sysprof_symbol_map_add_resolver (map, kernel);
  sysprof_symbol_map_add_resolver (map, elf);

  sysprof_symbol_map_resolve (map, reader);

  fd = sysprof_memfd_create ("decode-test");
  g_assert_cmpint (fd, !=, -1);

  r = sysprof_symbol_map_serialize (map, fd);
  g_assert_true (r);
  sysprof_symbol_map_free (map);

  /* Reset some state */
  sysprof_capture_reader_reset (reader);
  lseek (fd, 0, SEEK_SET);

  /* Now desrialize it */
  map = sysprof_symbol_map_new ();
  sysprof_symbol_map_deserialize (map, G_BYTE_ORDER, fd);

  /* Now try to print some stack traces */
  while (sysprof_capture_reader_peek_type (reader, &type))
    {
      if (type == SYSPROF_CAPTURE_FRAME_SAMPLE)
        {
          const SysprofCaptureSample *sample = NULL;

          if (!(sample = sysprof_capture_reader_read_sample (reader)))
            break;

          for (guint j = 0; j < sample->n_addrs; j++)
            {
              const gchar *name;
              GQuark tag;

              if (!(name = sysprof_symbol_map_lookup (map, sample->frame.time, sample->frame.pid, sample->addrs[j], &tag)))
                name = "Unknown symbol";

              g_print ("%u: %s\n", j, name);
            }

          g_print ("======\n");
        }
      else if (!sysprof_capture_reader_skip (reader))
        break;
    }


  sysprof_symbol_map_free (map);

  close (fd);
  g_main_loop_quit (main_loop);
  return NULL;
}

gint
main (gint argc,
      gchar *argv[])
{
  g_autoptr(SysprofCaptureReader) reader = NULL;

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

  main_loop = g_main_loop_new (NULL, FALSE);
  g_thread_new ("reader-thread", resolve_in_thread, reader);
  g_main_loop_run (main_loop);

  return 0;
}
