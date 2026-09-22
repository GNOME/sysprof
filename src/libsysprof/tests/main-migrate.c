/* main-migrate.c
 *
 * Copyright 2026 Christian Hergert <christian@sourceandstack.com>
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
#include <locale.h>
#include <libdex.h>

#include <sysprof.h>
#include <sysprof-capture.h>

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureReader, sysprof_capture_reader_unref)

typedef struct
{
  int first;
  int last;
} CpuRange;

typedef enum
{
  EVENT_PROCESS,
  EVENT_EXIT,
  EVENT_SAMPLE,
} EventType;

typedef struct
{
  int64_t   time;
  guint     order;
  int32_t   pid;
  int       cpu;
  EventType type;
} Event;

typedef enum
{
  CPU_KIND_NONE,
  CPU_KIND_PERFORMANCE,
  CPU_KIND_EFFICIENCY,
} CpuKind;

typedef struct
{
  int     cpu;
  CpuKind kind;
} PreviousCpu;

static gint
compare_cpu_range (gconstpointer a,
                   gconstpointer b)
{
  const CpuRange *range_a = a;
  const CpuRange *range_b = b;

  return (range_a->first > range_b->first) - (range_a->first < range_b->first);
}

static gboolean
parse_cpu_ranges (const char  *data,
                  gsize        length,
                  GArray     **ranges,
                  GError     **error)
{
  g_autofree char *text = NULL;
  g_auto(GStrv) parts = NULL;
  GArray *result = NULL;

  g_assert (data != NULL);
  g_assert (ranges != NULL);

  text = g_strndup (data, length);
  g_strstrip (text);

  if (*text == '\0')
    goto malformed;

  result = g_array_new (FALSE, FALSE, sizeof (CpuRange));
  parts = g_strsplit (text, ",", -1);

  for (guint i = 0; parts[i] != NULL; i++)
    {
      CpuRange range;
      char *endptr;
      guint64 first;
      guint64 last;

      if (!g_ascii_isdigit (*parts[i]))
        goto malformed;

      errno = 0;
      first = g_ascii_strtoull (parts[i], &endptr, 10);
      if (errno != 0 || endptr == parts[i] || first > G_MAXINT)
        goto malformed;

      last = first;
      if (*endptr == '-')
        {
          if (!g_ascii_isdigit (endptr[1]))
            goto malformed;

          errno = 0;
          last = g_ascii_strtoull (endptr + 1, &endptr, 10);
          if (errno != 0 || last > G_MAXINT)
            goto malformed;
        }

      if (*endptr != '\0' || first > last)
        goto malformed;

      range.first = first;
      range.last = last;
      g_array_append_val (result, range);
    }

  g_array_sort (result, compare_cpu_range);

  for (guint i = 1; i < result->len; i++)
    {
      CpuRange *previous = &g_array_index (result, CpuRange, i - 1);
      CpuRange *current = &g_array_index (result, CpuRange, i);

      if (current->first <= previous->last)
        goto malformed;
    }

  *ranges = result;
  return TRUE;

malformed:
  g_clear_pointer (&result, g_array_unref);
  g_set_error_literal (error,
                       G_IO_ERROR,
                       G_IO_ERROR_INVALID_DATA,
                       "CPU topology contains a malformed CPU list");
  return FALSE;
}

static gboolean
cpu_ranges_overlap (GArray *a,
                    GArray *b)
{
  guint a_index = 0;
  guint b_index = 0;

  g_assert (a != NULL);
  g_assert (b != NULL);

  while (a_index < a->len && b_index < b->len)
    {
      CpuRange *a_range = &g_array_index (a, CpuRange, a_index);
      CpuRange *b_range = &g_array_index (b, CpuRange, b_index);

      if (a_range->last < b_range->first)
        a_index++;
      else if (b_range->last < a_range->first)
        b_index++;
      else
        return TRUE;
    }

  return FALSE;
}

static CpuKind
classify_cpu (GArray *performance_cpus,
              GArray *efficiency_cpus,
              int     cpu)
{
  g_assert (performance_cpus != NULL);
  g_assert (efficiency_cpus != NULL);

  for (guint i = 0; i < performance_cpus->len; i++)
    {
      CpuRange *range = &g_array_index (performance_cpus, CpuRange, i);

      if (cpu < range->first)
        break;
      if (cpu <= range->last)
        return CPU_KIND_PERFORMANCE;
    }

  for (guint i = 0; i < efficiency_cpus->len; i++)
    {
      CpuRange *range = &g_array_index (efficiency_cpus, CpuRange, i);

      if (cpu < range->first)
        break;
      if (cpu <= range->last)
        return CPU_KIND_EFFICIENCY;
    }

  return CPU_KIND_NONE;
}

static gint
compare_event (gconstpointer a,
               gconstpointer b)
{
  const Event *event_a = a;
  const Event *event_b = b;

  if (event_a->time != event_b->time)
    return (event_a->time > event_b->time) - (event_a->time < event_b->time);

  return (event_a->order > event_b->order) - (event_a->order < event_b->order);
}

static char *
format_time (GDateTime *capture_time,
             int64_t    start_time,
             int64_t    frame_time)
{
  g_autoptr(GDateTime) frame_date = NULL;
  g_autofree char *date = NULL;
  int64_t offset;
  int64_t seconds;
  int64_t nanoseconds;
  double relative;

  offset = frame_time - start_time;

  if (capture_time == NULL)
    {
      relative = offset / 1000000000.0;
      if (relative < 0)
        relative = -relative;

      return g_strdup_printf ("%c%.9f", offset < 0 ? '-' : '+', relative);
    }

  seconds = offset / 1000000000;
  nanoseconds = offset % 1000000000;

  if (nanoseconds < 0)
    {
      seconds--;
      nanoseconds += 1000000000;
    }

  frame_date = g_date_time_add_seconds (capture_time, seconds);
  date = g_date_time_format (frame_date, "%Y-%m-%dT%H:%M:%S");

  return g_strdup_printf ("%s.%09" G_GINT64_FORMAT "Z", date, nanoseconds);
}

static gboolean
load_topology (SysprofDocument  *document,
               GArray          **performance_cpus,
               GArray          **efficiency_cpus,
               GError          **error)
{
  g_autoptr(SysprofDocumentFile) performance_file = NULL;
  g_autoptr(SysprofDocumentFile) efficiency_file = NULL;
  g_autoptr(GBytes) performance_bytes = NULL;
  g_autoptr(GBytes) efficiency_bytes = NULL;
  const char *performance_data;
  const char *efficiency_data;
  gsize performance_length;
  gsize efficiency_length;

  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (performance_cpus != NULL);
  g_assert (efficiency_cpus != NULL);

  performance_file = sysprof_document_lookup_file (document, "/sys/devices/cpu_core/cpus");
  efficiency_file = sysprof_document_lookup_file (document, "/sys/devices/cpu_atom/cpus");

  if (performance_file == NULL || efficiency_file == NULL)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_FOUND,
                           "Capture does not contain both hybrid CPU topology files");
      return FALSE;
    }

  if (!(performance_bytes = sysprof_document_file_dup_bytes (performance_file)) ||
      !(efficiency_bytes = sysprof_document_file_dup_bytes (efficiency_file)))
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Capture contains unreadable hybrid CPU topology files");
      return FALSE;
    }

  performance_data = g_bytes_get_data (performance_bytes, &performance_length);
  efficiency_data = g_bytes_get_data (efficiency_bytes, &efficiency_length);

  if (!parse_cpu_ranges (performance_data, performance_length, performance_cpus, error) ||
      !parse_cpu_ranges (efficiency_data, efficiency_length, efficiency_cpus, error))
    return FALSE;

  if (cpu_ranges_overlap (*performance_cpus, *efficiency_cpus))
    {
      g_clear_pointer (performance_cpus, g_array_unref);
      g_clear_pointer (efficiency_cpus, g_array_unref);
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Hybrid CPU topology lists overlap");
      return FALSE;
    }

  return TRUE;
}

static gboolean
collect_events (SysprofCaptureReader *reader,
                GArray               *events)
{
  SysprofCaptureFrameType type;
  guint order = 0;

  g_assert (reader != NULL);
  g_assert (events != NULL);

  while (sysprof_capture_reader_peek_type (reader, &type))
    {
      Event event;

      if (type == SYSPROF_CAPTURE_FRAME_PROCESS)
        {
          const SysprofCaptureProcess *process;

          if (!(process = sysprof_capture_reader_read_process (reader)))
            return FALSE;

          event = (Event) {
            .time = process->frame.time,
            .order = order++,
            .pid = process->frame.pid,
            .cpu = process->frame.cpu,
            .type = EVENT_PROCESS,
          };
          g_array_append_val (events, event);
        }
      else if (type == SYSPROF_CAPTURE_FRAME_EXIT)
        {
          const SysprofCaptureExit *exit;

          if (!(exit = sysprof_capture_reader_read_exit (reader)))
            return FALSE;

          event = (Event) {
            .time = exit->frame.time,
            .order = order++,
            .pid = exit->frame.pid,
            .cpu = exit->frame.cpu,
            .type = EVENT_EXIT,
          };
          g_array_append_val (events, event);
        }
      else if (type == SYSPROF_CAPTURE_FRAME_SAMPLE)
        {
          const SysprofCaptureSample *sample;

          if (!(sample = sysprof_capture_reader_read_sample (reader)))
            return FALSE;

          if (sample->frame.pid > 0 && sample->tid == sample->frame.pid)
            {
              event = (Event) {
                .time = sample->frame.time,
                .order = order++,
                .pid = sample->frame.pid,
                .cpu = sample->frame.cpu,
                .type = EVENT_SAMPLE,
              };
              g_array_append_val (events, event);
            }
          else
            order++;
        }
      else
        {
          if (!sysprof_capture_reader_skip (reader))
            return FALSE;
          order++;
        }
    }

  g_array_sort (events, compare_event);
  return TRUE;
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr(GDateTime) capture_time = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(SysprofDocumentLoader) loader = NULL;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(SysprofCaptureReader) reader = NULL;
  g_autoptr(GArray) performance_cpus = NULL;
  g_autoptr(GArray) efficiency_cpus = NULL;
  g_autoptr(GArray) events = NULL;
  g_autoptr(GHashTable) previous_cpus = NULL;
  int64_t start_time;

  dex_init ();

  setlocale (LC_ALL, "");

  if (argc != 2)
    {
      g_printerr ("usage: %s CAPTURE_FILE\n", argv[0]);
      return EXIT_FAILURE;
    }

  loader = sysprof_document_loader_new (argv[1]);
  sysprof_document_loader_set_symbolizer (loader, sysprof_no_symbolizer_get ());

  if (!(document = sysprof_document_loader_load (loader, NULL, &error)))
    {
      g_printerr ("Failed to open capture: %s\n", error->message);
      return EXIT_FAILURE;
    }

  if (!load_topology (document, &performance_cpus, &efficiency_cpus, &error))
    {
      g_printerr ("Cannot analyze main-thread migrations: %s\n", error->message);
      return EXIT_FAILURE;
    }

  if (!(reader = sysprof_capture_reader_new (argv[1])))
    {
      g_printerr ("Failed to open capture \"%s\".\n", argv[1]);
      return EXIT_FAILURE;
    }

  events = g_array_new (FALSE, FALSE, sizeof (Event));
  if (!collect_events (reader, events))
    {
      g_printerr ("Failed to read capture \"%s\".\n", argv[1]);
      return EXIT_FAILURE;
    }

  start_time = sysprof_capture_reader_get_start_time (reader);
  capture_time = g_date_time_new_from_iso8601 (sysprof_capture_reader_get_time (reader), NULL);
  previous_cpus = g_hash_table_new_full (NULL, NULL, NULL, g_free);

  for (guint i = 0; i < events->len; i++)
    {
      Event *event = &g_array_index (events, Event, i);
      CpuKind kind;
      PreviousCpu *previous;

      if (event->pid <= 0)
        continue;

      if (event->type != EVENT_SAMPLE)
        {
          g_hash_table_remove (previous_cpus, GINT_TO_POINTER (event->pid));
          continue;
        }

      kind = classify_cpu (performance_cpus, efficiency_cpus, event->cpu);
      if (kind == CPU_KIND_NONE)
        {
          g_hash_table_remove (previous_cpus, GINT_TO_POINTER (event->pid));
          continue;
        }

      previous = g_hash_table_lookup (previous_cpus, GINT_TO_POINTER (event->pid));
      if (previous != NULL && previous->kind == CPU_KIND_PERFORMANCE &&
          kind == CPU_KIND_EFFICIENCY)
        {
          g_autofree char *time = format_time (capture_time, start_time, event->time);

          g_print ("%s pid=%" G_GINT32_FORMAT " p-cpu=%d e-cpu=%d\n",
                   time, event->pid, previous->cpu, event->cpu);
        }

      previous = g_new (PreviousCpu, 1);
      previous->cpu = event->cpu;
      previous->kind = kind;
      g_hash_table_insert (previous_cpus, GINT_TO_POINTER (event->pid), previous);
    }

  return EXIT_SUCCESS;
}
