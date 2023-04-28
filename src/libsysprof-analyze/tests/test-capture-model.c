#include <errno.h>
#include <fcntl.h>

#include <sysprof-analyze.h>
#include <sysprof-capture.h>

int
main (int argc,
      char *argv[])
{
  SysprofDocument *document;
  const char *filename;
  GError *error = NULL;
  guint n_items;

  sysprof_clock_init ();

  if (argc < 2)
    {
      g_printerr ("usage: %s FILENAME\n", argv[0]);
      return 1;
    }

  filename = argv[1];

  if (!(document = sysprof_document_new (filename, &error)))
    {
      g_printerr ("Failed to load %s: %s\n",
                  filename, error->message);
      return 1;
    }

  n_items = g_list_model_get_n_items (G_LIST_MODEL (document));

  for (guint i = 0; i < n_items; i++)
    {
      SysprofDocumentFrame *frame = g_list_model_get_item (G_LIST_MODEL (document), i);

      g_print ("%"G_GINT64_FORMAT" [pid %d] [cpu %d] (type %s)",
               sysprof_document_frame_get_time (frame),
               sysprof_document_frame_get_pid (frame),
               sysprof_document_frame_get_cpu (frame),
               G_OBJECT_TYPE_NAME (frame));

      if (0) {}
      else if (SYSPROF_IS_DOCUMENT_LOG (frame))
        g_print (" domain=%s message=%s",
                 sysprof_document_log_get_domain (SYSPROF_DOCUMENT_LOG (frame)),
                 sysprof_document_log_get_message (SYSPROF_DOCUMENT_LOG (frame)));
      else if (SYSPROF_IS_DOCUMENT_MARK (frame))
        g_print (" group=%s name=%s message=%s",
                 sysprof_document_mark_get_group (SYSPROF_DOCUMENT_MARK (frame)),
                 sysprof_document_mark_get_name (SYSPROF_DOCUMENT_MARK (frame)),
                 sysprof_document_mark_get_message (SYSPROF_DOCUMENT_MARK (frame)));
      else if (SYSPROF_IS_DOCUMENT_PROCESS (frame))
        g_print (" cmdline=%s", sysprof_document_process_get_command_line (SYSPROF_DOCUMENT_PROCESS (frame)));
      else if (SYSPROF_IS_DOCUMENT_METADATA (frame))
        g_print (" id=%s value=%s",
                 sysprof_document_metadata_get_id (SYSPROF_DOCUMENT_METADATA (frame)),
                 sysprof_document_metadata_get_value (SYSPROF_DOCUMENT_METADATA (frame)));
      else if (SYSPROF_IS_DOCUMENT_FORK (frame))
        g_print (" child-pid=%d",
                 sysprof_document_fork_get_child_pid (SYSPROF_DOCUMENT_FORK (frame)));
      else if (SYSPROF_IS_DOCUMENT_ALLOCATION (frame))
        {
          if (sysprof_document_allocation_is_free (SYSPROF_DOCUMENT_ALLOCATION (frame)))
            g_print (" 0x%016"G_GINT64_MODIFIER"x: free",
                     sysprof_document_allocation_get_address (SYSPROF_DOCUMENT_ALLOCATION (frame)));
          else
            g_print (" 0x%016"G_GINT64_MODIFIER"x: allocate %"G_GUINT64_FORMAT,
                     sysprof_document_allocation_get_address (SYSPROF_DOCUMENT_ALLOCATION (frame)),
                     sysprof_document_allocation_get_size (SYSPROF_DOCUMENT_ALLOCATION (frame)));
        }

      g_print ("\n");

      g_clear_object (&frame);
    }

  g_printerr ("%u frames\n", n_items);

  g_clear_object (&document);

  return 0;
}
