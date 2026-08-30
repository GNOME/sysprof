/* mark-summary.c
 *
 * Copyright 2026 Christian Hergert
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"

#include <locale.h>
#include <math.h>

#include <glib.h>
#include <sysprof-capture.h>

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureReader,
                               sysprof_capture_reader_unref)

typedef struct
{
  char *group;
  char *name;
  GArray *durations;
  uint64_t point_count;
} MarkSummary;

typedef struct
{
  uint64_t count;
  uint64_t total;
  uint64_t minimum;
  uint64_t maximum;
  double mean;
  double median;
  uint64_t p95;
  double stddev;
} TimedStatistics;

static void
mark_summary_free (MarkSummary *summary)
{
  g_assert (summary != NULL);

  g_free (summary->group);
  g_free (summary->name);
  g_array_unref (summary->durations);
  g_free (summary);
}

static MarkSummary *
mark_summary_new (const char *group,
                  const char *name)
{
  MarkSummary *summary;

  g_assert (group != NULL);
  g_assert (name != NULL);

  summary = g_new0 (MarkSummary, 1);
  summary->group = g_strdup (group);
  summary->name = g_strdup (name);
  summary->durations = g_array_new (FALSE, FALSE, sizeof (uint64_t));

  return summary;
}

static int
compare_uint64 (const void *a,
                const void *b)
{
  const uint64_t value_a = *(const uint64_t *)a;
  const uint64_t value_b = *(const uint64_t *)b;

  return (value_a > value_b) - (value_a < value_b);
}

static int
compare_summaries (const void *a,
                   const void *b)
{
  const MarkSummary *summary_a = *(MarkSummary *const *)a;
  const MarkSummary *summary_b = *(MarkSummary *const *)b;
  int ret;

  if ((ret = strcmp (summary_a->group, summary_b->group)) != 0)
    return ret;

  return strcmp (summary_a->name, summary_b->name);
}

static void
calculate_statistics (MarkSummary     *summary,
                      TimedStatistics *statistics)
{
  long double sum = 0;
  long double squared_difference_sum = 0;
  uint64_t *values;
  gsize p95_index;

  g_assert (summary != NULL);
  g_assert (summary->durations->len > 0);
  g_assert (statistics != NULL);

  g_array_sort (summary->durations, compare_uint64);
  values = (uint64_t *)summary->durations->data;

  statistics->count = summary->durations->len;
  statistics->minimum = values[0];
  statistics->maximum = values[summary->durations->len - 1];

  for (guint i = 0; i < summary->durations->len; i++)
    sum += values[i];

  statistics->total = (uint64_t)sum;
  statistics->mean = (double)(sum / statistics->count);

  if ((summary->durations->len % 2) == 0)
    {
      const gsize middle = summary->durations->len / 2;

      statistics->median = ((double)values[middle - 1] / 2.0)
                           + ((double)values[middle] / 2.0);
    }
  else
    {
      statistics->median = values[summary->durations->len / 2];
    }

  p95_index = ((95 * (gsize)summary->durations->len) + 99) / 100;
  statistics->p95 = values[p95_index - 1];

  for (guint i = 0; i < summary->durations->len; i++)
    {
      const long double difference = values[i] - statistics->mean;

      squared_difference_sum += difference * difference;
    }

  statistics->stddev
      = sqrt ((double)(squared_difference_sum / statistics->count));
}

static char *
format_duration (double nanoseconds)
{
  const char *unit = "ns";
  double divisor = 1.0;

  if (nanoseconds >= 1000000000.0)
    {
      unit = "s";
      divisor = 1000000000.0;
    }
  else if (nanoseconds >= 1000000.0)
    {
      unit = "ms";
      divisor = 1000000.0;
    }
  else if (nanoseconds >= 1000.0)
    {
      unit = "µs";
      divisor = 1000.0;
    }

  return g_strdup_printf ("%.3f %s", nanoseconds / divisor, unit);
}

static void
print_table_cell (const char *str,
                  gsize       width,
                  gboolean    left_aligned)
{
  gsize length;
  gsize padding;

  g_assert (str != NULL);

  length = g_utf8_strlen (str, -1);
  padding = width > length ? width - length : 0;

  if (!left_aligned)
    for (gsize i = 0; i < padding; i++)
      g_print (" ");

  g_print ("%s", str);

  if (left_aligned)
    for (gsize i = 0; i < padding; i++)
      g_print (" ");
}

static void
print_csv_string (const char *str)
{
  g_assert (str != NULL);

  g_print ("\"");

  for (const char *iter = str; *iter != '\0'; iter++)
    {
      if (*iter == '"')
        g_print ("\"\"");
      else
        g_print ("%c", *iter);
    }

  g_print ("\"");
}

static void
print_ascii_double (double value)
{
  char buffer[G_ASCII_DTOSTR_BUF_SIZE];

  g_ascii_formatd (buffer, sizeof buffer, "%.3f", value);
  g_print ("%s", buffer);
}

static void
print_csv (GPtrArray *summaries)
{
  g_assert (summaries != NULL);

  g_print (
      "kind,group,name,count,total_ns,min_ns,max_ns,mean_ns,median_ns,p95_"
      "ns,stddev_ns\n");

  for (guint i = 0; i < summaries->len; i++)
    {
      MarkSummary *summary = g_ptr_array_index (summaries, i);
      TimedStatistics statistics = { 0 };

      if (summary->durations->len == 0)
        continue;

      calculate_statistics (summary, &statistics);
      g_print ("timed,");
      print_csv_string (summary->group);
      g_print (",");
      print_csv_string (summary->name);
      g_print (",%" G_GUINT64_FORMAT ",%" G_GUINT64_FORMAT
               ",%" G_GUINT64_FORMAT ",%" G_GUINT64_FORMAT ",",
               statistics.count, statistics.total, statistics.minimum,
               statistics.maximum);
      print_ascii_double (statistics.mean);
      g_print (",");
      print_ascii_double (statistics.median);
      g_print (",%" G_GUINT64_FORMAT ",", statistics.p95);
      print_ascii_double (statistics.stddev);
      g_print ("\n");
    }

  for (guint i = 0; i < summaries->len; i++)
    {
      MarkSummary *summary = g_ptr_array_index (summaries, i);

      if (summary->point_count == 0)
        continue;

      g_print ("point,");
      print_csv_string (summary->group);
      g_print (",");
      print_csv_string (summary->name);
      g_print (",%" G_GUINT64_FORMAT ",0,0,0,0,0,0,0\n", summary->point_count);
    }
}

static void
print_table (GPtrArray *summaries)
{
  gboolean have_points = FALSE;
  gboolean have_timed = FALSE;

  g_assert (summaries != NULL);

  for (guint i = 0; i < summaries->len; i++)
    {
      MarkSummary *summary = g_ptr_array_index (summaries, i);

      have_timed |= summary->durations->len > 0;
      have_points |= summary->point_count > 0;
    }

  if (!have_timed && !have_points)
    {
      g_print ("No marks found.\n");
      return;
    }

  if (have_timed)
    {
      g_print ("Timed Marks\n");
      print_table_cell ("Group", 24, TRUE);
      g_print (" ");
      print_table_cell ("Name", 40, TRUE);
      g_print (" ");
      print_table_cell ("Count", 8, FALSE);
      g_print (" ");
      print_table_cell ("Total", 13, FALSE);
      g_print (" ");
      print_table_cell ("Min", 13, FALSE);
      g_print (" ");
      print_table_cell ("Max", 13, FALSE);
      g_print (" ");
      print_table_cell ("Mean", 13, FALSE);
      g_print (" ");
      print_table_cell ("Median", 13, FALSE);
      g_print (" ");
      print_table_cell ("P95", 13, FALSE);
      g_print (" ");
      print_table_cell ("Stddev", 13, FALSE);
      g_print ("\n");

      for (guint i = 0; i < summaries->len; i++)
        {
          g_autofree char *count = NULL;
          g_autofree char *maximum = NULL;
          g_autofree char *mean = NULL;
          g_autofree char *median = NULL;
          g_autofree char *minimum = NULL;
          g_autofree char *p95 = NULL;
          g_autofree char *stddev = NULL;
          g_autofree char *total = NULL;
          MarkSummary *summary = g_ptr_array_index (summaries, i);
          TimedStatistics statistics = { 0 };

          if (summary->durations->len == 0)
            continue;

          calculate_statistics (summary, &statistics);
          total = format_duration (statistics.total);
          minimum = format_duration (statistics.minimum);
          maximum = format_duration (statistics.maximum);
          mean = format_duration (statistics.mean);
          median = format_duration (statistics.median);
          p95 = format_duration (statistics.p95);
          stddev = format_duration (statistics.stddev);
          count = g_strdup_printf ("%" G_GUINT64_FORMAT, statistics.count);

          print_table_cell (summary->group, 24, TRUE);
          g_print (" ");
          print_table_cell (summary->name, 40, TRUE);
          g_print (" ");
          print_table_cell (count, 8, FALSE);
          g_print (" ");
          print_table_cell (total, 13, FALSE);
          g_print (" ");
          print_table_cell (minimum, 13, FALSE);
          g_print (" ");
          print_table_cell (maximum, 13, FALSE);
          g_print (" ");
          print_table_cell (mean, 13, FALSE);
          g_print (" ");
          print_table_cell (median, 13, FALSE);
          g_print (" ");
          print_table_cell (p95, 13, FALSE);
          g_print (" ");
          print_table_cell (stddev, 13, FALSE);
          g_print ("\n");
        }
    }

  if (have_points)
    {
      if (have_timed)
        g_print ("\n");

      g_print ("Point Marks\n");
      print_table_cell ("Group", 24, TRUE);
      g_print (" ");
      print_table_cell ("Name", 40, TRUE);
      g_print (" ");
      print_table_cell ("Count", 8, FALSE);
      g_print ("\n");

      for (guint i = 0; i < summaries->len; i++)
        {
          MarkSummary *summary = g_ptr_array_index (summaries, i);

          if (summary->point_count > 0)
            {
              g_autofree char *count = NULL;

              count = g_strdup_printf ("%" G_GUINT64_FORMAT, summary->point_count);
              print_table_cell (summary->group, 24, TRUE);
              g_print (" ");
              print_table_cell (summary->name, 40, TRUE);
              g_print (" ");
              print_table_cell (count, 8, FALSE);
              g_print ("\n");
            }
        }
    }
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr (GHashTable) by_key = NULL;
  g_autoptr (GOptionContext) context = NULL;
  g_autoptr (GPtrArray) summaries = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (SysprofCaptureReader) reader = NULL;
  gboolean csv = FALSE;
  GOptionEntry entries[]
      = { { "csv", 0, 0, G_OPTION_ARG_NONE, &csv, "Output CSV", NULL },
          { NULL } };
  SysprofCaptureFrameType type;

  setlocale (LC_ALL, "");

  context = g_option_context_new ("CAPTURE_FILE");
  g_option_context_add_main_entries (context, entries, NULL);

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("%s\n", error->message);
      return EXIT_FAILURE;
    }

  if (argc != 2)
    {
      g_autofree char *help = g_option_context_get_help (context, TRUE, NULL);

      g_printerr ("%s", help);
      return EXIT_FAILURE;
    }

  if (!(reader = sysprof_capture_reader_new (argv[1])))
    {
      g_printerr ("Failed to open capture \"%s\".\n", argv[1]);
      return EXIT_FAILURE;
    }

  by_key = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  summaries
      = g_ptr_array_new_with_free_func ((GDestroyNotify)mark_summary_free);

  while (sysprof_capture_reader_peek_type (reader, &type))
    {
      const SysprofCaptureMark *mark;
      g_autofree char *key = NULL;
      MarkSummary *summary;

      if (type != SYSPROF_CAPTURE_FRAME_MARK)
        {
          if (!sysprof_capture_reader_skip (reader))
            {
              g_printerr ("Failed to read capture \"%s\".\n", argv[1]);
              return EXIT_FAILURE;
            }

          continue;
        }

      if (!(mark = sysprof_capture_reader_read_mark (reader)))
        {
          g_printerr ("Failed to read capture \"%s\".\n", argv[1]);
          return EXIT_FAILURE;
        }

      key = g_strconcat (mark->group, "\xff", mark->name, NULL);

      if (!(summary = g_hash_table_lookup (by_key, key)))
        {
          summary = mark_summary_new (mark->group, mark->name);
          g_hash_table_insert (by_key, g_steal_pointer (&key), summary);
          g_ptr_array_add (summaries, summary);
        }

      if (mark->duration == 0)
        summary->point_count++;
      else
        {
          const uint64_t duration = mark->duration;

          g_array_append_val (summary->durations, duration);
        }
    }

  g_ptr_array_sort (summaries, compare_summaries);

  if (csv)
    print_csv (summaries);
  else
    print_table (summaries);

  return EXIT_SUCCESS;
}
