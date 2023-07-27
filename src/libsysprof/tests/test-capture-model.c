#include <errno.h>
#include <fcntl.h>

#include <sysprof.h>

#include "sysprof-document-private.h"

int
main (int argc,
      char *argv[])
{
  g_autoptr(SysprofDocumentLoader) loader = NULL;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(GError) error = NULL;
  const char *filename;
  guint n_items;

  sysprof_clock_init ();

  if (argc < 2)
    {
      g_printerr ("usage: %s FILENAME\n", argv[0]);
      return 1;
    }

  filename = argv[1];

  loader = sysprof_document_loader_new (filename);
  sysprof_document_loader_set_symbolizer (loader, sysprof_no_symbolizer_get ());

  if (!(document = sysprof_document_loader_load (loader, NULL, &error)))
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
      else if (SYSPROF_IS_DOCUMENT_FILE_CHUNK (frame))
        g_print (" file-chunk path=%s len=%u",
                 sysprof_document_file_chunk_get_path (SYSPROF_DOCUMENT_FILE_CHUNK (frame)),
                 sysprof_document_file_chunk_get_size (SYSPROF_DOCUMENT_FILE_CHUNK (frame)));
      else if (SYSPROF_IS_DOCUMENT_FORK (frame))
        g_print (" child-pid=%d",
                 sysprof_document_fork_get_child_pid (SYSPROF_DOCUMENT_FORK (frame)));
      else if (SYSPROF_IS_DOCUMENT_OVERLAY (frame))
        g_print (" layer=%u source=%s destination=%s",
                 sysprof_document_overlay_get_layer (SYSPROF_DOCUMENT_OVERLAY (frame)),
                 sysprof_document_overlay_get_source (SYSPROF_DOCUMENT_OVERLAY (frame)),
                 sysprof_document_overlay_get_destination (SYSPROF_DOCUMENT_OVERLAY (frame)));
      else if (SYSPROF_IS_DOCUMENT_MMAP (frame))
        g_print (" begin=0x%"G_GINT64_MODIFIER"x end=0x%"G_GINT64_MODIFIER"x offset=0x%"G_GINT64_MODIFIER"x path=%s",
                 sysprof_document_mmap_get_start_address (SYSPROF_DOCUMENT_MMAP (frame)),
                 sysprof_document_mmap_get_end_address (SYSPROF_DOCUMENT_MMAP (frame)),
                 sysprof_document_mmap_get_file_offset (SYSPROF_DOCUMENT_MMAP (frame)),
                 sysprof_document_mmap_get_file (SYSPROF_DOCUMENT_MMAP (frame)));
      else if (SYSPROF_IS_DOCUMENT_JITMAP (frame))
        g_print (" n_jitmaps=%u",
                 sysprof_document_jitmap_get_size (SYSPROF_DOCUMENT_JITMAP (frame)));
      else if (SYSPROF_IS_DOCUMENT_DBUS_MESSAGE (frame))
        g_print (" message-length=%u serial=0x%x sender=%s destination=%s",
                 sysprof_document_dbus_message_get_message_length (SYSPROF_DOCUMENT_DBUS_MESSAGE (frame)),
                 sysprof_document_dbus_message_get_serial (SYSPROF_DOCUMENT_DBUS_MESSAGE (frame)),
                 sysprof_document_dbus_message_get_sender (SYSPROF_DOCUMENT_DBUS_MESSAGE (frame)),
                 sysprof_document_dbus_message_get_destination (SYSPROF_DOCUMENT_DBUS_MESSAGE (frame)));
      else if (SYSPROF_IS_DOCUMENT_CTRDEF (frame))
        {
          guint n_counters = sysprof_document_ctrdef_get_n_counters (SYSPROF_DOCUMENT_CTRDEF (frame));

          g_print (" n_counters=%u", n_counters);

          for (guint j = 0; j < n_counters; j++)
            {
              const char *category, *name;
              guint id, type;

              sysprof_document_ctrdef_get_counter (SYSPROF_DOCUMENT_CTRDEF (frame),
                                                   j, &id, &type, &category, &name, NULL);
              g_print (" [%u<%s>:%s.%s]",
                       id,
                       type == SYSPROF_CAPTURE_COUNTER_INT64 ? "i64" : "f64",
                       category, name);
            }
        }
      else if (SYSPROF_IS_DOCUMENT_CTRSET (frame))
        {
          guint n_values = sysprof_document_ctrset_get_n_values (SYSPROF_DOCUMENT_CTRSET (frame));

          g_print (" counters=[");
          for (guint j = 0; j < n_values; j++)
            {
              guint id;
              guint8 raw[8];

              sysprof_document_ctrset_get_raw_value (SYSPROF_DOCUMENT_CTRSET (frame), j, &id, raw);

              g_print ("%u", id);
              if (j+1 != n_values)
                g_print (", ");
            }
          g_print ("]");
        }
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

      if (SYSPROF_IS_DOCUMENT_TRACEABLE (frame))
        g_print (" stack-depth=%u",
                 sysprof_document_traceable_get_stack_depth (SYSPROF_DOCUMENT_TRACEABLE (frame)));

      g_print ("\n");

      g_clear_object (&frame);
    }

  g_printerr ("%u frames\n", n_items);

  return 0;
}
