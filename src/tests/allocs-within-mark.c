/* allocs-within-mark.c
 *
 * Copyright 2020 Christian Hergert <chergert@redhat.com>
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
#include <glib/gi18n.h>
#include <locale.h>
#include <stdlib.h>
#include <sysprof.h>

typedef struct
{
  gint64 begin;
  gint64 end;
  gint64 allocated;
} Interval;

static gint
compare_interval (gconstpointer a,
                  gconstpointer b)
{
  const Interval *aptr = a;
  const Interval *bptr = b;

  if (aptr->begin < bptr->begin)
    return -1;
  else if (aptr->begin > bptr->begin)
    return 1;

  if (aptr->end < bptr->end)
    return -1;
  else if (aptr->end > bptr->end)
    return 1;

  return 0;
}

static gint
find_time_cb (gconstpointer key,
              gconstpointer b)
{
  const Interval *keyptr = key;
  const Interval *bptr = b;

  if (keyptr->begin >= bptr->begin && keyptr->end <= bptr->end)
    return 0;

  return compare_interval (key, b);
}

static inline Interval *
find_time (GArray *ar,
           gint64  time)
{
  Interval key = {time, time, 0};

  return bsearch (&key, ar->data, ar->len, sizeof (Interval), find_time_cb);
}

static void
allocs_within_mark (SysprofCaptureReader *reader,
                    const gchar          *category,
                    const gchar          *name)
{
  g_autoptr(GArray) intervals = g_array_new (FALSE, FALSE, sizeof (Interval));
  SysprofCaptureFrameType type;
  gint64 begin;
  struct {
    gint64 total;
    gint64 min;
    gint64 max;
  } st = { 0, G_MAXINT64, G_MININT64 };

  g_assert (reader);
  g_assert (category);
  g_assert (name);

  begin = sysprof_capture_reader_get_start_time (reader);

  while (sysprof_capture_reader_peek_type (reader, &type))
    {
      if (type == SYSPROF_CAPTURE_FRAME_MARK)
        {
          const SysprofCaptureMark *ev;
          Interval *iv;

          if (!(ev = sysprof_capture_reader_read_mark (reader)))
            break;

          if (strcmp (category, ev->group) != 0 ||
              strcmp (name, ev->name) != 0)
            continue;

          g_array_set_size (intervals, intervals->len + 1);

          iv = &g_array_index (intervals, Interval, intervals->len - 1);
          iv->begin = ev->frame.time;
          iv->end = ev->frame.time + ev->duration;
          iv->allocated = 0;
        }
      else if (!sysprof_capture_reader_skip (reader))
        break;
    }

  g_array_sort (intervals, compare_interval);

  sysprof_capture_reader_reset (reader);

  while (sysprof_capture_reader_peek_type (reader, &type))
    {
      if (type == SYSPROF_CAPTURE_FRAME_ALLOCATION)
        {
          const SysprofCaptureAllocation *ev;
          Interval *iv;

          if (!(ev = sysprof_capture_reader_read_allocation (reader)))
            break;

          if (ev->alloc_size <= 0)
            continue;

          if (!(iv = find_time (intervals, ev->frame.time)))
            continue;

          iv->allocated += ev->alloc_size;
        }
      else if (!sysprof_capture_reader_skip (reader))
        break;
    }

  for (guint i = 0; i < intervals->len; i++)
    {
      const Interval *iv = &g_array_index (intervals, Interval, i);
      g_autofree gchar *size = NULL;
      gdouble t1;
      gdouble t2;

      if (iv->allocated <= 0)
        continue;

      st.total += iv->allocated;
      st.min = MIN (st.min, iv->allocated);
      st.max = MAX (st.max, iv->allocated);

      size = g_format_size_full (iv->allocated, G_FORMAT_SIZE_IEC_UNITS);

      t1 = (iv->begin - begin) / (gdouble)SYSPROF_NSEC_PER_SEC;
      t2 = (iv->end - begin) / (gdouble)SYSPROF_NSEC_PER_SEC;

      g_print ("%lf-%lf: %s\n", t1, t2, size);
    }

  if (intervals->len)
    {
      const Interval *iv = &g_array_index (intervals, Interval, intervals->len/2);
      g_autofree gchar *minstr = g_format_size_full (st.min, G_FORMAT_SIZE_IEC_UNITS);
      g_autofree gchar *maxstr = g_format_size_full (st.max, G_FORMAT_SIZE_IEC_UNITS);
      g_autofree gchar *avgstr = g_format_size_full (st.total/(gdouble)intervals->len, G_FORMAT_SIZE_IEC_UNITS);
      g_autofree gchar *medstr = g_format_size_full (iv->allocated, G_FORMAT_SIZE_IEC_UNITS);

      g_print ("Min: %s, Max: %s, Avg: %s, Median: %s\n", minstr, maxstr, avgstr, medstr);
    }
}

gint
main (gint   argc,
      gchar *argv[])
{
  SysprofCaptureReader *reader;
  const gchar *filename;
  const gchar *category;
  const gchar *name;

  if (argc < 4)
    {
      g_printerr ("usage: %s FILENAME MARK_CATEGORY MARK_NAME\n", argv[0]);
      return EXIT_FAILURE;
    }

  filename = argv[1];
  category = argv[2];
  name = argv[3];

  /* Set up gettext translations */
  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  if (!(reader = sysprof_capture_reader_new (filename)))
    {
      int errsv = errno;
      g_printerr ("%s\n", g_strerror (errsv));
      return EXIT_FAILURE;
    }

  allocs_within_mark (reader, category, name);

  sysprof_capture_reader_unref (reader);

  return EXIT_SUCCESS;
}
