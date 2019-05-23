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

#include <stdio.h>
#include <stdlib.h>
#include <sysprof-capture.h>

#define NSEC_PER_SEC G_GINT64_CONSTANT(1000000000)

gint
main (gint argc,
      gchar *argv[])
{
  SysprofCaptureReader *reader;
  SysprofCaptureFrameType type;
  GHashTable *ctrtypes;
  GError *error = NULL;
  gint64 begin_time;
  gint64 end_time;

  if (argc != 2)
    {
      g_printerr ("usage: %s FILENAME\n", argv[0]);
      return EXIT_FAILURE;
    }

  reader = sysprof_capture_reader_new (argv[1], &error);
  ctrtypes = g_hash_table_new (NULL, NULL);

  begin_time = sysprof_capture_reader_get_start_time (reader);

#define SET_CTR_TYPE(i,t) g_hash_table_insert(ctrtypes, GINT_TO_POINTER(i), GINT_TO_POINTER(t))
#define GET_CTR_TYPE(i) GPOINTER_TO_INT(g_hash_table_lookup(ctrtypes, GINT_TO_POINTER(i)))

  if (reader == NULL)
    {
      g_printerr ("%s\n", error->message);
      return EXIT_FAILURE;
    }

  begin_time = sysprof_capture_reader_get_start_time (reader);
  end_time = sysprof_capture_reader_get_end_time (reader);

  g_print ("Capture Time Range: %"G_GUINT64_FORMAT" to %"G_GUINT64_FORMAT" (%lf)\n",
           begin_time, end_time, (end_time - begin_time) / (gdouble)NSEC_PER_SEC);

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

        case SYSPROF_CAPTURE_FRAME_JITMAP:
          {
            g_autoptr(GHashTable) ret = sysprof_capture_reader_read_jitmap (reader);
            GHashTableIter iter;
            SysprofCaptureAddress addr;
            const gchar *str;

            if (ret == NULL)
              return EXIT_FAILURE;

            g_print ("JITMAP:\n");

            g_hash_table_iter_init (&iter, ret);
            while (g_hash_table_iter_next (&iter, (gpointer *)&addr, (gpointer *)&str))
              g_print ("  "SYSPROF_CAPTURE_ADDRESS_FORMAT" : %s\n", addr, str);

            break;
          }

        case SYSPROF_CAPTURE_FRAME_MAP:
          {
            const SysprofCaptureMap *map = sysprof_capture_reader_read_map (reader);

            g_print ("MAP: pid=%d time=%"G_GINT64_FORMAT"\n"
                     "   start  = %"G_GUINT64_FORMAT"\n"
                     "     end  = %"G_GUINT64_FORMAT"\n"
                     "   offset = %"G_GUINT64_FORMAT"\n"
                     "    inode = %"G_GUINT64_FORMAT"\n"
                     " filename = %s\n",
                     map->frame.pid, map->frame.time,
                     map->start, map->end, map->offset, map->inode, map->filename);

            break;
          }

        case SYSPROF_CAPTURE_FRAME_MARK:
          {
            const SysprofCaptureMark *mark = sysprof_capture_reader_read_mark (reader);
            gdouble ptime = (mark->frame.time - begin_time) / (gdouble)NSEC_PER_SEC;

            g_print ("MARK: pid=%d time=%"G_GINT64_FORMAT" (%lf)\n"
                     "    group = %s\n"
                     "     name = %s\n"
                     " duration = %"G_GUINT64_FORMAT"\n"
                     "  message = %s\n",
                     mark->frame.pid, mark->frame.time, ptime,
                     mark->group, mark->name, mark->duration, mark->message);

            break;
          }

        case SYSPROF_CAPTURE_FRAME_METADATA:
          {
            const SysprofCaptureMetadata *metadata = sysprof_capture_reader_read_metadata (reader);
            gdouble ptime = (metadata->frame.time - begin_time) / (gdouble)NSEC_PER_SEC;

            g_print ("METADATA: pid=%d time=%"G_GINT64_FORMAT" (%lf)\n"
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

            g_print ("PROCESS: pid=%d cmdline=%s time=%"G_GINT64_FORMAT"\n", pr->frame.pid, pr->cmdline, pr->frame.time);

            break;
          }

        case SYSPROF_CAPTURE_FRAME_SAMPLE:
          {
            const SysprofCaptureSample *s =  sysprof_capture_reader_read_sample (reader);
            gdouble ptime = (s->frame.time - begin_time) / (gdouble)NSEC_PER_SEC;
            guint i;

            g_print ("SAMPLE: pid=%d time=%"G_GINT64_FORMAT" (%lf)\n", s->frame.pid, s->frame.time, ptime);

            for (i = 0; i < s->n_addrs; i++)
              g_print ("  "SYSPROF_CAPTURE_ADDRESS_FORMAT"\n", s->addrs[i]);

            break;
          }

        case SYSPROF_CAPTURE_FRAME_TIMESTAMP:
          {
            const SysprofCaptureTimestamp *ts =  sysprof_capture_reader_read_timestamp (reader);
            g_print ("TIMESTAMP: pid=%d time=%"G_GINT64_FORMAT"\n", ts->frame.pid, ts->frame.time);
            break;
          }

        case SYSPROF_CAPTURE_FRAME_CTRDEF:
          {
            const SysprofCaptureCounterDefine *def = sysprof_capture_reader_read_counter_define (reader);
            gdouble ptime = (def->frame.time - begin_time) / (gdouble)NSEC_PER_SEC;
            guint i;

            g_print ("NEW COUNTERS: pid=%d time=%"G_GINT64_FORMAT" (%lf)\n", def->frame.pid, def->frame.time, ptime);

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
            gdouble ptime = (set->frame.time - begin_time) / (gdouble)NSEC_PER_SEC;
            guint i;

            g_print ("SET COUNTERS: pid=%d time=%"G_GINT64_FORMAT" (%lf)\n", set->frame.pid, set->frame.time, ptime);

            for (i = 0; i < set->n_values; i++)
              {
                const SysprofCaptureCounterValues *values = &set->values[i];
                guint j;

                for (j = 0; j < G_N_ELEMENTS (values->ids); j++)
                  {
                    if (values->ids[j])
                      {
                        if (GET_CTR_TYPE (values->ids[j]) == SYSPROF_CAPTURE_COUNTER_INT64)
                          g_print ("  COUNTER(%d): %"G_GINT64_FORMAT"\n",
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
