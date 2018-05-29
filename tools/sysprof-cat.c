/* sysprof-cat.c
 *
 * Copyright © 2018 Christian Hergert <chergert@redhat.com>
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

#include "config.h"

#include <glib/gstdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "capture/sp-capture-reader.h"
#include "capture/sp-capture-writer.h"
#include "util/sp-platform.h"

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

static GArray *translate_table[N_TRANSLATE];

static void
translate_table_clear (guint table)
{
  g_clear_pointer (&translate_table[table], g_array_unref);
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
translate_table_sort (guint table)
{
  if (translate_table[table])
    g_array_sort (translate_table[table], compare_by_src);
}

static void
translate_table_add (guint   table,
                     guint64 src,
                     guint64 dst)
{
  TranslateItem item = { src, dst };

  if (translate_table[table] == NULL)
    translate_table[table] = g_array_new (FALSE, FALSE, sizeof (TranslateItem));

  g_array_append_val (translate_table[table], item);
}

static guint64
translate_table_translate (guint   table,
                           guint64 src)
{
  const TranslateItem *item;
  TranslateItem key = { src, 0 };

  if (!translate_table[table])
    return src;

  item = bsearch (&key,
                  translate_table[table]->data,
                  translate_table[table]->len,
                  sizeof (TranslateItem),
                  compare_by_src);

  return item != NULL ? item->dst : src;
}

gint
main (gint   argc,
      gchar *argv[])
{
  g_autoptr(SpCaptureWriter) writer = NULL;
  g_autofree gchar *contents = NULL;
  g_autofree gchar *tmpname = NULL;
  gint64 first_start_time = G_MAXINT64;
  gint64 end_time = -1;
  gsize len;
  gint fd;

  if (argc == 1)
    return 0;

  if (isatty (STDOUT_FILENO))
    {
      g_printerr ("stdout is a TTY, refusing to write binary data to stdout.\n");
      return EXIT_FAILURE;
    }

  for (guint i = 1; i < argc; i++)
    {
      if (!g_file_test (argv[i], G_FILE_TEST_IS_REGULAR))
        {
          g_printerr ("\"%s\" is not a regular file.\n", argv[i]);
          return EXIT_FAILURE;
        }
    }

  if (-1 == (fd = g_file_open_tmp (".sysprof-cat-XXXXXX", &tmpname, NULL)))
    {
      g_printerr ("Failed to create memfd for capture file.\n");
      return EXIT_FAILURE;
    }

  writer = sp_capture_writer_new_from_fd (fd, 4096*4);

  for (guint i = 1; i < argc; i++)
    {
      g_autoptr(GError) error = NULL;
      g_autoptr(SpCaptureReader) reader = sp_capture_reader_new (argv[i], &error);
      SpCaptureFrameType type;
      gint64 start_time;

      if (reader == NULL)
        {
          g_printerr ("Failed to create reader for \"%s\": %s\n",
                      argv[i], error->message);
          return EXIT_FAILURE;
        }

      translate_table_clear (TRANSLATE_CTR);
      translate_table_clear (TRANSLATE_ADDR);

      start_time = sp_capture_reader_get_start_time (reader);

      if (start_time < first_start_time)
        first_start_time = start_time;

      while (sp_capture_reader_peek_type (reader, &type))
        {
          SpCaptureFrame fr;

          if (sp_capture_reader_peek_frame (reader, &fr))
            {
              if (fr.time > end_time)
                end_time = fr.time;
            }

          switch (type)
            {
            case SP_CAPTURE_FRAME_TIMESTAMP:
              {
                const SpCaptureTimestamp *frame;

                if (!(frame = sp_capture_reader_read_timestamp (reader)))
                  goto panic;

                sp_capture_writer_add_timestamp (writer,
                                                 frame->frame.time,
                                                 frame->frame.cpu,
                                                 frame->frame.pid);
                break;
              }

            case SP_CAPTURE_FRAME_MAP:
              {
                const SpCaptureMap *frame;

                if (!(frame = sp_capture_reader_read_map (reader)))
                  goto panic;

                sp_capture_writer_add_map (writer,
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

            case SP_CAPTURE_FRAME_MARK:
              {
                const SpCaptureMark *frame;

                if (!(frame = sp_capture_reader_read_mark (reader)))
                  goto panic;

                sp_capture_writer_add_mark (writer,
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

            case SP_CAPTURE_FRAME_PROCESS:
              {
                const SpCaptureProcess *frame;

                if (!(frame = sp_capture_reader_read_process (reader)))
                  goto panic;

                sp_capture_writer_add_process (writer,
                                               frame->frame.time,
                                               frame->frame.cpu,
                                               frame->frame.pid,
                                               frame->cmdline);
                break;
              }

            case SP_CAPTURE_FRAME_FORK:
              {
                const SpCaptureFork *frame;

                if (!(frame = sp_capture_reader_read_fork (reader)))
                  goto panic;

                sp_capture_writer_add_fork (writer,
                                            frame->frame.time,
                                            frame->frame.cpu,
                                            frame->frame.pid,
                                            frame->child_pid);
                break;
              }

            case SP_CAPTURE_FRAME_EXIT:
              {
                const SpCaptureExit *frame;

                if (!(frame = sp_capture_reader_read_exit (reader)))
                  goto panic;

                sp_capture_writer_add_exit (writer,
                                            frame->frame.time,
                                            frame->frame.cpu,
                                            frame->frame.pid);
                break;
              }

            case SP_CAPTURE_FRAME_JITMAP:
              {
                GHashTable *jitmap;
                GHashTableIter iter;
                const gchar *name;
                guint64 addr;

                if (!(jitmap = sp_capture_reader_read_jitmap (reader)))
                  goto panic;

                g_hash_table_iter_init (&iter, jitmap);
                while (g_hash_table_iter_next (&iter, (gpointer *)&addr, (gpointer *)&name))
                  {
                    guint64 replace = sp_capture_writer_add_jitmap (writer, name);
                    /* We need to keep a table of replacement addresses so that
                     * we can translate the samples into the destination address
                     * space that we synthesized for the address identifier.
                     */
                    translate_table_add (TRANSLATE_ADDR, addr, replace);
                  }

                translate_table_sort (TRANSLATE_ADDR);

                g_hash_table_unref (jitmap);

                break;
              }

            case SP_CAPTURE_FRAME_SAMPLE:
              {
                const SpCaptureSample *frame;

                if (!(frame = sp_capture_reader_read_sample (reader)))
                  goto panic;

                {
                  SpCaptureAddress addrs[frame->n_addrs];

                  for (guint z = 0; z < frame->n_addrs; z++)
                    addrs[z] = translate_table_translate (TRANSLATE_ADDR, frame->addrs[z]);

                  sp_capture_writer_add_sample (writer,
                                                frame->frame.time,
                                                frame->frame.cpu,
                                                frame->frame.pid,
                                                addrs,
                                                frame->n_addrs);
                }

                break;
              }

            case SP_CAPTURE_FRAME_CTRDEF:
              {
                const SpCaptureFrameCounterDefine *frame;

                if (!(frame = sp_capture_reader_read_counter_define (reader)))
                  goto panic;

                {
                  g_autoptr(GArray) counter = g_array_new (FALSE, FALSE, sizeof (SpCaptureCounter));

                  for (guint z = 0; z < frame->n_counters; z++)
                    {
                      SpCaptureCounter c = frame->counters[z];
                      guint src = c.id;

                      c.id = sp_capture_writer_request_counter (writer, 1);

                      if (c.id != src)
                        translate_table_add (TRANSLATE_CTR, src, c.id);

                      g_array_append_val (counter, c);
                    }

                  sp_capture_writer_define_counters (writer,
                                                     frame->frame.time,
                                                     frame->frame.cpu,
                                                     frame->frame.pid,
                                                     (gpointer)counter->data,
                                                     counter->len);

                  translate_table_sort (TRANSLATE_CTR);
                }

                break;
              }

            case SP_CAPTURE_FRAME_CTRSET:
              {
                const SpCaptureFrameCounterSet *frame;

                if (!(frame = sp_capture_reader_read_counter_set (reader)))
                  goto panic;

                {
                  g_autoptr(GArray) ids = g_array_new (FALSE, FALSE, sizeof (guint));
                  g_autoptr(GArray) values = g_array_new (FALSE, FALSE, sizeof (SpCaptureCounterValue));

                  for (guint z = 0; z < frame->n_values; z++)
                    {
                      const SpCaptureCounterValues *v = &frame->values[z];

                      for (guint y = 0; y < G_N_ELEMENTS (v->ids); y++)
                        {
                          if (v->ids[y])
                            {
                              guint dst = translate_table_translate (TRANSLATE_CTR, v->ids[y]);
                              SpCaptureCounterValue value = v->values[y];

                              g_array_append_val (ids, dst);
                              g_array_append_val (values, value);
                            }
                        }
                    }

                  g_assert (ids->len == values->len);

                  sp_capture_writer_set_counters (writer,
                                                  frame->frame.time,
                                                  frame->frame.cpu,
                                                  frame->frame.pid,
                                                  (const guint *)(gpointer)ids->data,
                                                  (const SpCaptureCounterValue *)(gpointer)values->data,
                                                  ids->len);
                }

                break;
              }

            default:
              break;
            }
        }
    }

  sp_capture_writer_flush (writer);

  /* do this after flushing as it uses pwrite() to replace data */
  _sp_capture_writer_set_time_range (writer, first_start_time, end_time);

  if (g_file_get_contents (tmpname, &contents, &len, NULL))
    write (STDOUT_FILENO, contents, len);

  g_unlink (tmpname);

  return EXIT_SUCCESS;

panic:
  g_printerr ("There was a failure to read the stream.\n");

  if (tmpname)
    g_unlink (tmpname);

  return EXIT_FAILURE;
}
