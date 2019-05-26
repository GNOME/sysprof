/* sysprof-cat.c
 *
 * Copyright 2018-2019 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sysprof-cat"

#include "config.h"

#include <glib/gstdio.h>
#include <stdlib.h>
#include <sysprof-capture.h>
#include <unistd.h>

typedef struct
{
  guint64 src;
  guint64 dst;
} TranslateItem;

enum {
  TRANSLATE_ADDR,
  TRANSLATE_CTR,
  N_TRANSLATE
};

static void
translate_table_clear (GArray **tables,
                       guint    table)
{
  g_clear_pointer (&tables[table], g_array_unref);
}

static gint
compare_by_src (gconstpointer a,
                gconstpointer b)
{
  const TranslateItem *itema = a;
  const TranslateItem *itemb = a;

  if (itema->src < itemb->src)
    return -1;
  else if (itema->src > itemb->src)
    return 1;
  else 
    return 0;
}

static void
translate_table_sort (GArray **tables,
                      guint    table)
{
  if (tables[table])
    g_array_sort (tables[table], compare_by_src);
}

static void
translate_table_add (GArray  **tables,
                     guint     table,
                     guint64   src,
                     guint64   dst)
{
  TranslateItem item = { src, dst };

  if (tables[table] == NULL)
    tables[table] = g_array_new (FALSE, FALSE, sizeof (TranslateItem));

  g_array_append_val (tables[table], item);
}

static guint64
translate_table_translate (GArray  **tables,
                           guint     table,
                           guint64   src)
{
  const TranslateItem *item;
  TranslateItem key = { src, 0 };

  if (!tables[table])
    return src;

  item = bsearch (&key,
                  tables[table]->data,
                  tables[table]->len,
                  sizeof (TranslateItem),
                  compare_by_src);

  return item != NULL ? item->dst : src;
}

gboolean
sysprof_capture_writer_cat (SysprofCaptureWriter  *self,
                            SysprofCaptureReader  *reader,
                            GError               **error)
{
  GArray *tables[N_TRANSLATE] = { NULL };
  SysprofCaptureFrameType type;
  gint64 start_time;
  gint64 first_start_time = G_MAXINT64;
  gint64 end_time = -1;

  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (reader != NULL, FALSE);

  translate_table_clear (tables, TRANSLATE_CTR);
  translate_table_clear (tables, TRANSLATE_ADDR);

  start_time = sysprof_capture_reader_get_start_time (reader);

  if (start_time < first_start_time)
    first_start_time = start_time;

  while (sysprof_capture_reader_peek_type (reader, &type))
    {
      SysprofCaptureFrame fr;

      if (sysprof_capture_reader_peek_frame (reader, &fr))
        {
          if (fr.time > end_time)
            end_time = fr.time;
        }

      switch (type)
        {
        case SYSPROF_CAPTURE_FRAME_TIMESTAMP:
          {
            const SysprofCaptureTimestamp *frame;

            if (!(frame = sysprof_capture_reader_read_timestamp (reader)))
              goto panic;

            sysprof_capture_writer_add_timestamp (self,
                                                  frame->frame.time,
                                                  frame->frame.cpu,
                                                  frame->frame.pid);
            break;
          }

        case SYSPROF_CAPTURE_FRAME_LOG:
          {
            const SysprofCaptureLog *frame;

            if (!(frame = sysprof_capture_reader_read_log (reader)))
              goto panic;

            sysprof_capture_writer_add_log (self,
                                            frame->frame.time,
                                            frame->frame.cpu,
                                            frame->frame.pid,
                                            frame->severity,
                                            frame->domain,
                                            frame->message);
            break;
          }

        case SYSPROF_CAPTURE_FRAME_MAP:
          {
            const SysprofCaptureMap *frame;

            if (!(frame = sysprof_capture_reader_read_map (reader)))
              goto panic;

            sysprof_capture_writer_add_map (self,
                                            frame->frame.time,
                                            frame->frame.cpu,
                                            frame->frame.pid,
                                            frame->start,
                                            frame->end,
                                            frame->offset,
                                            frame->inode,
                                            frame->filename);
            break;
          }

        case SYSPROF_CAPTURE_FRAME_MARK:
          {
            const SysprofCaptureMark *frame;

            if (!(frame = sysprof_capture_reader_read_mark (reader)))
              goto panic;

            sysprof_capture_writer_add_mark (self,
                                             frame->frame.time,
                                             frame->frame.cpu,
                                             frame->frame.pid,
                                             frame->duration,
                                             frame->group,
                                             frame->name,
                                             frame->message);

            if (frame->frame.time + frame->duration > end_time)
              end_time = frame->frame.time + frame->duration;

            break;
          }

        case SYSPROF_CAPTURE_FRAME_PROCESS:
          {
            const SysprofCaptureProcess *frame;

            if (!(frame = sysprof_capture_reader_read_process (reader)))
              goto panic;

            sysprof_capture_writer_add_process (self,
                                                frame->frame.time,
                                                frame->frame.cpu,
                                                frame->frame.pid,
                                                frame->cmdline);
            break;
          }

        case SYSPROF_CAPTURE_FRAME_FORK:
          {
            const SysprofCaptureFork *frame;

            if (!(frame = sysprof_capture_reader_read_fork (reader)))
              goto panic;

            sysprof_capture_writer_add_fork (self,
                                             frame->frame.time,
                                             frame->frame.cpu,
                                             frame->frame.pid,
                                             frame->child_pid);
            break;
          }

        case SYSPROF_CAPTURE_FRAME_EXIT:
          {
            const SysprofCaptureExit *frame;

            if (!(frame = sysprof_capture_reader_read_exit (reader)))
              goto panic;

            sysprof_capture_writer_add_exit (self,
                                             frame->frame.time,
                                             frame->frame.cpu,
                                             frame->frame.pid);
            break;
          }

        case SYSPROF_CAPTURE_FRAME_METADATA:
          {
            const SysprofCaptureMetadata *frame;

            if (!(frame = sysprof_capture_reader_read_metadata (reader)))
              goto panic;

            sysprof_capture_writer_add_metadata (self,
                                                 frame->frame.time,
                                                 frame->frame.cpu,
                                                 frame->frame.pid,
                                                 frame->id,
                                                 frame->metadata,
                                                 frame->frame.len - G_STRUCT_OFFSET (SysprofCaptureMetadata, metadata));
            break;
          }

        case SYSPROF_CAPTURE_FRAME_JITMAP:
          {
            GHashTable *jitmap;
            GHashTableIter iter;
            const gchar *name;
            guint64 addr;

            if (!(jitmap = sysprof_capture_reader_read_jitmap (reader)))
              goto panic;

            g_hash_table_iter_init (&iter, jitmap);
            while (g_hash_table_iter_next (&iter, (gpointer *)&addr, (gpointer *)&name))
              {
                guint64 replace = sysprof_capture_writer_add_jitmap (self, name);
                /* We need to keep a table of replacement addresses so that
                 * we can translate the samples into the destination address
                 * space that we synthesized for the address identifier.
                 */
                translate_table_add (tables, TRANSLATE_ADDR, addr, replace);
              }

            translate_table_sort (tables, TRANSLATE_ADDR);

            g_hash_table_unref (jitmap);

            break;
          }

        case SYSPROF_CAPTURE_FRAME_SAMPLE:
          {
            const SysprofCaptureSample *frame;

            if (!(frame = sysprof_capture_reader_read_sample (reader)))
              goto panic;

            {
              SysprofCaptureAddress addrs[frame->n_addrs];

              for (guint z = 0; z < frame->n_addrs; z++)
                addrs[z] = translate_table_translate (tables, TRANSLATE_ADDR, frame->addrs[z]);

              sysprof_capture_writer_add_sample (self,
                                                 frame->frame.time,
                                                 frame->frame.cpu,
                                                 frame->frame.pid,
                                                 frame->tid,
                                                 addrs,
                                                 frame->n_addrs);
            }

            break;
          }

        case SYSPROF_CAPTURE_FRAME_CTRDEF:
          {
            const SysprofCaptureCounterDefine *frame;

            if (!(frame = sysprof_capture_reader_read_counter_define (reader)))
              goto panic;

            {
              g_autoptr(GArray) counter = g_array_new (FALSE, FALSE, sizeof (SysprofCaptureCounter));

              for (guint z = 0; z < frame->n_counters; z++)
                {
                  SysprofCaptureCounter c = frame->counters[z];
                  guint src = c.id;

                  c.id = sysprof_capture_writer_request_counter (self, 1);

                  if (c.id != src)
                    translate_table_add (tables, TRANSLATE_CTR, src, c.id);

                  g_array_append_val (counter, c);
                }

              sysprof_capture_writer_define_counters (self,
                                                      frame->frame.time,
                                                      frame->frame.cpu,
                                                      frame->frame.pid,
                                                      (gpointer)counter->data,
                                                      counter->len);

              translate_table_sort (tables, TRANSLATE_CTR);
            }

            break;
          }

        case SYSPROF_CAPTURE_FRAME_CTRSET:
          {
            const SysprofCaptureCounterSet *frame;

            if (!(frame = sysprof_capture_reader_read_counter_set (reader)))
              goto panic;

            {
              g_autoptr(GArray) ids = g_array_new (FALSE, FALSE, sizeof (guint));
              g_autoptr(GArray) values = g_array_new (FALSE, FALSE, sizeof (SysprofCaptureCounterValue));

              for (guint z = 0; z < frame->n_values; z++)
                {
                  const SysprofCaptureCounterValues *v = &frame->values[z];

                  for (guint y = 0; y < G_N_ELEMENTS (v->ids); y++)
                    {
                      if (v->ids[y])
                        {
                          guint dst = translate_table_translate (tables, TRANSLATE_CTR, v->ids[y]);
                          SysprofCaptureCounterValue value = v->values[y];

                          g_array_append_val (ids, dst);
                          g_array_append_val (values, value);
                        }
                    }
                }

              g_assert (ids->len == values->len);

              sysprof_capture_writer_set_counters (self,
                                                   frame->frame.time,
                                                   frame->frame.cpu,
                                                   frame->frame.pid,
                                                   (const guint *)(gpointer)ids->data,
                                                   (const SysprofCaptureCounterValue *)(gpointer)values->data,
                                                   ids->len);
            }

            break;
          }

        default:
          break;
        }
    }

  sysprof_capture_writer_flush (self);

  /* do this after flushing as it uses pwrite() to replace data */
  _sysprof_capture_writer_set_time_range (self, first_start_time, end_time);

  translate_table_clear (tables, TRANSLATE_ADDR);
  translate_table_clear (tables, TRANSLATE_CTR);

  return TRUE;

panic:
  g_set_error (error,
               G_FILE_ERROR,
               G_FILE_ERROR_FAILED,
               "Failed to write data");

  translate_table_clear (tables, TRANSLATE_ADDR);
  translate_table_clear (tables, TRANSLATE_CTR);

  return FALSE;
}
