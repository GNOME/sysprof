/* memory-samples.c
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

#include <glib.h>
#include <sysprof-capture.h>

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureReader, sysprof_capture_reader_unref)

int
main (int   argc,
      char *argv[])
{
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(SysprofCaptureReader) reader = NULL;
  g_autoptr(GError) error = NULL;
  int pid = -1;
  int tid = -1;
  gint64 event = 0;
  double from = 0;
  double to = G_MAXDOUBLE;
  guint64 count = 0;
  SysprofCaptureFrame frame;
  GOptionEntry entries[] = {
    { "pid", 0, 0, G_OPTION_ARG_INT, &pid, "Filter process", "PID" },
    { "tid", 0, 0, G_OPTION_ARG_INT, &tid, "Filter thread", "TID" },
    { "event", 0, 0, G_OPTION_ARG_INT64, &event, "Filter capture-local event ID", "ID" },
    { "from", 0, 0, G_OPTION_ARG_DOUBLE, &from, "Start in capture-relative seconds", "SECONDS" },
    { "to", 0, 0, G_OPTION_ARG_DOUBLE, &to, "End in capture-relative seconds (exclusive)", "SECONDS" },
    { NULL }
  };

  context = g_option_context_new ("CAPTURE - inspect memory-access samples and event descriptions");
  g_option_context_add_main_entries (context, entries, NULL);

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("%s\n", error->message);

      return 1;
    }

  if (argc != 2 ||
      from < 0 ||
      to < from ||
      event < 0)
    {
      g_printerr ("Usage: %s [--pid PID] [--tid TID] [--event ID] [--from S --to S] CAPTURE\n", argv[0]);

      return 1;
    }

  if (!(reader = sysprof_capture_reader_new (argv[1])))
    {
      g_printerr ("Cannot read capture: %s\n", g_strerror (errno));

      return 1;
    }

  while (sysprof_capture_reader_peek_frame (reader, &frame))
    {
      if (frame.type == SYSPROF_CAPTURE_FRAME_METADATA)
        {
          const SysprofCaptureMetadata *metadata = sysprof_capture_reader_read_metadata (reader);

          if (metadata == NULL)
            goto invalid;

          if (g_str_has_prefix (metadata->id, SYSPROF_CAPTURE_MEMORY_EVENT_PREFIX))
            g_print ("description %s\n%s\n", metadata->id, metadata->metadata);
        }
      else if (frame.type == SYSPROF_CAPTURE_FRAME_LOG)
        {
          const SysprofCaptureLog *log = sysprof_capture_reader_read_log (reader);

          if (log == NULL)
            goto invalid;

          if (g_str_equal (log->domain, "Memory Sampler"))
            g_print ("diagnostic %s\n", log->message);
        }
      else if (frame.type == SYSPROF_CAPTURE_FRAME_MEMORY_SAMPLE)
        {
          const SysprofCaptureMemorySample *sample = sysprof_capture_reader_read_memory_sample (reader);
          double seconds;

          if (sample == NULL)
            goto invalid;

          seconds = (sample->frame.time - sysprof_capture_reader_get_start_time (reader)) / 1e9;

          if ((pid != -1 && pid != sample->frame.pid) ||
              (tid != -1 && tid != sample->tid) ||
              (event != 0 && event != sample->event_id) || seconds < from || seconds >= to)
            continue;

          count++;
          g_print ("sample time=%.9f pid=%d tid=%d cpu=%d event=%" G_GUINT64_FORMAT
                   " flags=0x%x ip=0x%" G_GINT64_MODIFIER "x address=0x%" G_GINT64_MODIFIER "x"
                   " period=%" G_GUINT64_FORMAT " weight-type=%u weight=%" G_GUINT64_FORMAT
                   " data-src=0x%" G_GINT64_MODIFIER "x enabled=%" G_GUINT64_FORMAT
                   " running=%" G_GUINT64_FORMAT " stack=",
                   seconds, sample->frame.pid, sample->tid, sample->frame.cpu, sample->event_id,
                   sample->flags, sample->ip, sample->address, sample->period, sample->weight_type,
                   sample->weight, sample->data_src, sample->time_enabled, sample->time_running);

          for (guint i = 0; i < sample->n_addrs; i++)
            g_print ("%s0x%" G_GINT64_MODIFIER "x", i ? "," : "", sample->addrs[i]);

          g_print ("\n");
        }
      else if (!sysprof_capture_reader_skip (reader))
        goto invalid;
    }

  g_print ("memory-samples=%" G_GUINT64_FORMAT " (selected samples, not total accesses)\n", count);

  return 0;

invalid:
  g_printerr ("Invalid or truncated capture frame\n");

  return 1;
}
