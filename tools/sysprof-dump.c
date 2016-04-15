/* sysprof-dump.c
 *
 * Copyright (C) 2016 Christian Hergert <chergert@redhat.com>
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
 */

#include <stdio.h>
#include <stdlib.h>

#include "sp-capture-reader.h"

gint
main (gint argc,
      gchar *argv[])
{
  SpCaptureReader *reader;
  SpCaptureFrameType type;
  GHashTable *ctrtypes;
  GError *error = NULL;

  if (argc != 2)
    {
      g_printerr ("usage: %s FILENAME\n", argv[0]);
      return EXIT_FAILURE;
    }

  reader = sp_capture_reader_new (argv[1], &error);
  ctrtypes = g_hash_table_new (NULL, NULL);

#define SET_CTR_TYPE(i,t) g_hash_table_insert(ctrtypes, GINT_TO_POINTER(i), GINT_TO_POINTER(t))
#define GET_CTR_TYPE(i) GPOINTER_TO_INT(g_hash_table_lookup(ctrtypes, GINT_TO_POINTER(i)))

  if (reader == NULL)
    {
      g_printerr ("%s\n", error->message);
      return EXIT_FAILURE;
    }

  while (sp_capture_reader_peek_type (reader, &type))
    {
      switch (type)
        {
        case SP_CAPTURE_FRAME_EXIT:
          {
            const SpCaptureExit *ex = sp_capture_reader_read_exit (reader);

            if (ex == NULL)
              return EXIT_FAILURE;

            g_print ("EXIT: pid=%d\n", ex->frame.pid);

            break;
          }

        case SP_CAPTURE_FRAME_FORK:
          {
            const SpCaptureFork *fk = sp_capture_reader_read_fork (reader);

            if (fk == NULL)
              return EXIT_FAILURE;

            g_print ("FORK: pid=%d child_pid=%d\n", fk->frame.pid, fk->child_pid);

            break;
          }

        case SP_CAPTURE_FRAME_JITMAP:
          {
            g_autoptr(GHashTable) ret = sp_capture_reader_read_jitmap (reader);
            GHashTableIter iter;
            SpCaptureAddress addr;
            const gchar *str;

            if (ret == NULL)
              return EXIT_FAILURE;

            g_print ("JITMAP:\n");

            g_hash_table_iter_init (&iter, ret);
            while (g_hash_table_iter_next (&iter, (gpointer *)&addr, (gpointer *)&str))
              g_print ("  "SP_CAPTURE_ADDRESS_FORMAT" : %s\n", addr, str);

            break;
          }

        case SP_CAPTURE_FRAME_MAP:
          {
            const SpCaptureMap *map = sp_capture_reader_read_map (reader);

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

        case SP_CAPTURE_FRAME_PROCESS:
          {
            const SpCaptureProcess *pr = sp_capture_reader_read_process (reader);

            if (pr == NULL)
              perror ("Failed to read process");

            g_print ("PROCESS: pid=%d cmdline=%s time=%"G_GINT64_FORMAT"\n", pr->frame.pid, pr->cmdline, pr->frame.time);

            break;
          }

        case SP_CAPTURE_FRAME_SAMPLE:
          {
            const SpCaptureSample *s =  sp_capture_reader_read_sample (reader);
            guint i;

            g_print ("SAMPLE: pid=%d time=%"G_GINT64_FORMAT"\n", s->frame.pid, s->frame.time);

            for (i = 0; i < s->n_addrs; i++)
              g_print ("  "SP_CAPTURE_ADDRESS_FORMAT"\n", s->addrs[i]);

            break;
          }

        case SP_CAPTURE_FRAME_TIMESTAMP:
          {
            const SpCaptureTimestamp *ts =  sp_capture_reader_read_timestamp (reader);
            g_print ("TIMESTAMP: pid=%d time=%"G_GINT64_FORMAT"\n", ts->frame.pid, ts->frame.time);
            break;
          }

        case SP_CAPTURE_FRAME_CTRDEF:
          {
            const SpCaptureFrameCounterDefine *def = sp_capture_reader_read_counter_define (reader);
            guint i;

            g_print ("NEW COUNTERS: pid=%d time=%"G_GINT64_FORMAT"\n", def->frame.pid, def->frame.time);

            for (i = 0; i < def->n_counters; i++)
              {
                const SpCaptureCounter *ctr = &def->counters[i];

                SET_CTR_TYPE (ctr->id, ctr->type);

                g_print ("  COUNTER(%d): %s\n           %s\n           %s\n\n",
                         ctr->id,
                         ctr->category,
                         ctr->name,
                         ctr->description);
              }
          }
          break;

        case SP_CAPTURE_FRAME_CTRSET:
          {
            const SpCaptureFrameCounterSet *set = sp_capture_reader_read_counter_set (reader);
            guint i;

            g_print ("SET COUNTERS: pid=%d time=%"G_GINT64_FORMAT"\n", set->frame.pid, set->frame.time);

            for (i = 0; i < set->n_values; i++)
              {
                const SpCaptureCounterValues *values = &set->values[i];
                guint j;

                for (j = 0; j < G_N_ELEMENTS (values->ids); j++)
                  {
                    if (values->ids[j])
                      {
                        if (GET_CTR_TYPE (values->ids[j]) == SP_CAPTURE_COUNTER_INT64)
                          g_print ("  COUNTER(%d): %"G_GINT64_FORMAT"\n",
                                   values->ids[j],
                                   values->values[j].v64);
                        else if (GET_CTR_TYPE (values->ids[j]) == SP_CAPTURE_COUNTER_DOUBLE)
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
          if (!sp_capture_reader_skip (reader))
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
