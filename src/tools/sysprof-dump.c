/* sysprof-dump.c
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"

#include <errno.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysprof-capture.h>

static gboolean list_files = FALSE;
static const GOptionEntry main_entries[] = {
  { "list-files", 'l', 0, G_OPTION_ARG_NONE, &list_files, "List files within the capture" },
  { 0 }
};

gint
main (gint argc,
      gchar *argv[])
{
  g_autoptr(GOptionContext) context = g_option_context_new ("- dump capture data");
  g_autoptr(GError) error = NULL;
  SysprofCaptureReader *reader;
  SysprofCaptureFrameType type;
  GHashTable *ctrtypes;
  gint64 begin_time;
  gint64 end_time;

  g_option_context_add_main_entries (context, main_entries, NULL);
  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("%s\n", error->message);
      return EXIT_FAILURE;
    }

  if (argc != 2)
    {
      g_printerr ("usage: %s FILENAME\n", argv[0]);
      return EXIT_FAILURE;
    }

  if ((reader = sysprof_capture_reader_new (argv[1])) == NULL)
    {
      int errsv = errno;
      g_printerr ("%s\n", g_strerror (errsv));
      return EXIT_FAILURE;
    }

  if (list_files)
    {
      g_autofree const gchar **files = sysprof_capture_reader_list_files (reader);

      for (gsize i = 0; files[i]; i++)
        g_print ("%s\n", files[i]);

      return EXIT_SUCCESS;
    }

  ctrtypes = g_hash_table_new (NULL, NULL);

  begin_time = sysprof_capture_reader_get_start_time (reader);

#define SET_CTR_TYPE(i,t) g_hash_table_insert(ctrtypes, GINT_TO_POINTER(i), GINT_TO_POINTER(t))
#define GET_CTR_TYPE(i) GPOINTER_TO_INT(g_hash_table_lookup(ctrtypes, GINT_TO_POINTER(i)))

  begin_time = sysprof_capture_reader_get_start_time (reader);
  end_time = sysprof_capture_reader_get_end_time (reader);

  g_print ("Capture Time Range: %" G_GUINT64_FORMAT " to %" G_GUINT64_FORMAT " (%lf)\n",
           begin_time, end_time, (end_time - begin_time) / (gdouble)SYSPROF_NSEC_PER_SEC);

  while (sysprof_capture_reader_peek_type (reader, &type))
    {
      switch (type)
        {
        case SYSPROF_CAPTURE_FRAME_EXIT:
          {
            const SysprofCaptureExit *ex = sysprof_capture_reader_read_exit (reader);

            if (ex == NULL)
              return EXIT_FAILURE;

            g_print ("EXIT: pid=%d\n", ex->frame.pid);

            break;
          }

        case SYSPROF_CAPTURE_FRAME_FORK:
          {
            const SysprofCaptureFork *fk = sysprof_capture_reader_read_fork (reader);

            if (fk == NULL)
              return EXIT_FAILURE;

            g_print ("FORK: pid=%d child_pid=%d\n", fk->frame.pid, fk->child_pid);

            break;
          }

        case SYSPROF_CAPTURE_FRAME_OVERLAY:
          {
            const SysprofCaptureOverlay *pr = sysprof_capture_reader_read_overlay (reader);
            const char *src = pr->data;
            const char *dst = &pr->data[pr->src_len+1];

            if (pr == NULL)
              return EXIT_FAILURE;

            g_print ("OVERLAY: pid=%d layer=%u src=%s dst=%s\n", pr->frame.pid, pr->layer, src, dst);

            break;
          }

        case SYSPROF_CAPTURE_FRAME_JITMAP:
          {
            const SysprofCaptureJitmap *jitmap = sysprof_capture_reader_read_jitmap (reader);
            SysprofCaptureJitmapIter iter;
            SysprofCaptureAddress addr;
            const gchar *str;

            if (jitmap == NULL)
              return EXIT_FAILURE;

            g_print ("JITMAP:\n");

            sysprof_capture_jitmap_iter_init (&iter, jitmap);
            while (sysprof_capture_jitmap_iter_next (&iter, &addr, &str))
              g_print ("  " SYSPROF_CAPTURE_ADDRESS_FORMAT " : %s\n", addr, str);

            break;
          }

        case SYSPROF_CAPTURE_FRAME_LOG:
          {
            const SysprofCaptureLog *log = sysprof_capture_reader_read_log (reader);
            gdouble ptime = (log->frame.time - begin_time) / (gdouble)SYSPROF_NSEC_PER_SEC;

            g_print ("LOG: pid=%d time=%" G_GINT64_FORMAT " (%lf)\n"
                     " severity = %d\n"
                     "   domain = %s\n"
                     "  message = %s\n",
                     log->frame.pid, log->frame.time, ptime,
                     log->severity, log->domain, log->message);

            break;
          }

        case SYSPROF_CAPTURE_FRAME_MAP:
          {
            const SysprofCaptureMap *map = sysprof_capture_reader_read_map (reader);

            g_print ("MAP: pid=%d time=%" G_GINT64_FORMAT "\n"
                     "   start  = %" G_GUINT64_FORMAT "\n"
                     "     end  = %" G_GUINT64_FORMAT "\n"
                     "   offset = %" G_GUINT64_FORMAT "\n"
                     "    inode = %" G_GUINT64_FORMAT "\n"
                     " filename = %s\n",
                     map->frame.pid, map->frame.time,
                     map->start, map->end, map->offset, map->inode, map->filename);

            break;
          }

        case SYSPROF_CAPTURE_FRAME_FILE_CHUNK:
          {
            const SysprofCaptureFileChunk *file_chunk = sysprof_capture_reader_read_file (reader);
            gdouble ptime = (file_chunk->frame.time - begin_time) / (gdouble)SYSPROF_NSEC_PER_SEC;

            g_print ("FILE_CHUNK: pid=%d time=%" G_GINT64_FORMAT " (%lf)\n"
                     "     path = %s\n"
                     "  is_last = %d\n"
                     "    bytes = %d\n",
                     file_chunk->frame.pid, file_chunk->frame.time, ptime,
                     file_chunk->path, file_chunk->is_last, file_chunk->len);

            break;
          }

        case SYSPROF_CAPTURE_FRAME_MARK:
          {
            const SysprofCaptureMark *mark = sysprof_capture_reader_read_mark (reader);
            gdouble ptime = (mark->frame.time - begin_time) / (gdouble)SYSPROF_NSEC_PER_SEC;

            g_print ("MARK: pid=%d time=%" G_GINT64_FORMAT " (%lf)\n"
                     "    group = %s\n"
                     "     name = %s\n"
                     " duration = %" G_GUINT64_FORMAT "\n"
                     "  message = %s\n",
                     mark->frame.pid, mark->frame.time, ptime,
                     mark->group, mark->name, mark->duration, mark->message);

            break;
          }

        case SYSPROF_CAPTURE_FRAME_METADATA:
          {
            const SysprofCaptureMetadata *metadata = sysprof_capture_reader_read_metadata (reader);
            gdouble ptime = (metadata->frame.time - begin_time) / (gdouble)SYSPROF_NSEC_PER_SEC;

            g_print ("METADATA: pid=%d time=%" G_GINT64_FORMAT " (%lf)\n"
                     "       id = %s\n"
                     "\"\"\"\n%s\n\"\"\"\n",
                     metadata->frame.pid, metadata->frame.time, ptime,
                     metadata->id, metadata->metadata);

            break;
          }

        case SYSPROF_CAPTURE_FRAME_PROCESS:
          {
            const SysprofCaptureProcess *pr = sysprof_capture_reader_read_process (reader);

            if (pr == NULL)
              perror ("Failed to read process");

            g_print ("PROCESS: pid=%d cmdline=%s time=%" G_GINT64_FORMAT "\n", pr->frame.pid, pr->cmdline, pr->frame.time);

            break;
          }

        case SYSPROF_CAPTURE_FRAME_SAMPLE:
          {
            const SysprofCaptureSample *s =  sysprof_capture_reader_read_sample (reader);
            gdouble ptime = (s->frame.time - begin_time) / (gdouble)SYSPROF_NSEC_PER_SEC;
            guint i;

            g_print ("SAMPLE: pid=%d time=%" G_GINT64_FORMAT " (%lf)\n", s->frame.pid, s->frame.time, ptime);

            for (i = 0; i < s->n_addrs; i++)
              g_print ("  " SYSPROF_CAPTURE_ADDRESS_FORMAT "\n", s->addrs[i]);

            break;
          }

        case SYSPROF_CAPTURE_FRAME_TIMESTAMP:
          {
            const SysprofCaptureTimestamp *ts =  sysprof_capture_reader_read_timestamp (reader);
            g_print ("TIMESTAMP: pid=%d time=%" G_GINT64_FORMAT "\n", ts->frame.pid, ts->frame.time);
            break;
          }

        case SYSPROF_CAPTURE_FRAME_CTRDEF:
          {
            const SysprofCaptureCounterDefine *def = sysprof_capture_reader_read_counter_define (reader);
            gdouble ptime = (def->frame.time - begin_time) / (gdouble)SYSPROF_NSEC_PER_SEC;
            guint i;

            g_print ("NEW COUNTERS: pid=%d time=%" G_GINT64_FORMAT " (%lf)\n", def->frame.pid, def->frame.time, ptime);

            for (i = 0; i < def->n_counters; i++)
              {
                const SysprofCaptureCounter *ctr = &def->counters[i];

                SET_CTR_TYPE (ctr->id, ctr->type);

                g_print ("  COUNTER(%d): %s\n"
                         "              %s\n"
                         "              %s\n"
                         "\n",
                         ctr->id,
                         ctr->category,
                         ctr->name,
                         ctr->description);
              }
          }
          break;

        case SYSPROF_CAPTURE_FRAME_CTRSET:
          {
            const SysprofCaptureCounterSet *set = sysprof_capture_reader_read_counter_set (reader);
            gdouble ptime = (set->frame.time - begin_time) / (gdouble)SYSPROF_NSEC_PER_SEC;
            guint i;

            g_print ("SET COUNTERS: pid=%d time=%" G_GINT64_FORMAT " (%lf)\n", set->frame.pid, set->frame.time, ptime);

            for (i = 0; i < set->n_values; i++)
              {
                const SysprofCaptureCounterValues *values = &set->values[i];
                guint j;

                for (j = 0; j < G_N_ELEMENTS (values->ids); j++)
                  {
                    if (values->ids[j])
                      {
                        if (GET_CTR_TYPE (values->ids[j]) == SYSPROF_CAPTURE_COUNTER_INT64)
                          g_print ("  COUNTER(%d): %" G_GINT64_FORMAT "\n",
                                   values->ids[j],
                                   values->values[j].v64);
                        else if (GET_CTR_TYPE (values->ids[j]) == SYSPROF_CAPTURE_COUNTER_DOUBLE)
                          g_print ("  COUNTER(%d): %lf\n",
                                   values->ids[j],
                                   values->values[j].vdbl);
                      }
                  }
              }
          }
          break;

        case SYSPROF_CAPTURE_FRAME_ALLOCATION:
          {
            const SysprofCaptureAllocation *ev = sysprof_capture_reader_read_allocation (reader);
            gdouble ptime = (ev->frame.time - begin_time) / (gdouble)SYSPROF_NSEC_PER_SEC;

            g_print ("%s: pid=%d tid=%d addr=0x%" G_GINT64_MODIFIER "x size=%" G_GINT64_FORMAT " time=%" G_GINT64_FORMAT " (%lf)\n",
                     ev->alloc_size > 0 ? "ALLOC" : "FREE",
                     ev->frame.pid, ev->tid,
                     ev->alloc_addr, ev->alloc_size,
                     ev->frame.time, ptime);
          }
          break;

        default:
          g_print ("Skipping unknown frame type: (%d): ", type);
          if (!sysprof_capture_reader_skip (reader))
            {
              g_print ("Failed\n");
              return EXIT_FAILURE;
            }
          g_print ("Success\n");
          break;
        }
    }

  return EXIT_SUCCESS;
}
