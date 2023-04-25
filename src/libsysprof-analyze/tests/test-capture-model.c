#include <errno.h>
#include <fcntl.h>
#include <sysprof-analyze.h>

int
main (int argc,
      char *argv[])
{
  SysprofCaptureModel *model;
  const char *filename;
  GError *error = NULL;
  guint n_items;
  int fd;

  sysprof_clock_init ();

  if (argc < 2)
    {
      g_printerr ("usage: %s FILENAME\n", argv[0]);
      return 1;
    }

  filename = argv[1];
  fd = open (filename, O_RDONLY|O_CLOEXEC);

  if (fd == -1)
    {
      g_printerr ("Failed to open %s: %s\n",
                  filename, g_strerror (errno));
      return 1;
    }

  if (!(model = sysprof_capture_model_new_from_fd (fd, &error)))
    {
      g_printerr ("Failed to load %s: %s\n",
                  filename, error->message);
      return 1;
    }

  n_items = g_list_model_get_n_items (G_LIST_MODEL (model));

  g_print ("%u frames\n", n_items);

  for (guint i = 0; i < n_items; i++)
    {
      SysprofCaptureFrameObject *obj = g_list_model_get_item (G_LIST_MODEL (model), i);

      g_clear_object (&obj);
    }

  close (fd);
  g_clear_object (&model);

  return 0;
}
