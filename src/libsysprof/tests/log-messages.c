/* log-messages.c
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

#include <glib.h>
#include <sysprof-capture.h>

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureReader,
                               sysprof_capture_reader_unref)

static const char *
severity_to_string (GLogLevelFlags severity)
{
  if (severity & G_LOG_LEVEL_ERROR)
    return "ERROR";
  if (severity & G_LOG_LEVEL_CRITICAL)
    return "CRITICAL";
  if (severity & G_LOG_LEVEL_WARNING)
    return "WARNING";
  if (severity & G_LOG_LEVEL_MESSAGE)
    return "MESSAGE";
  if (severity & G_LOG_LEVEL_INFO)
    return "INFO";
  if (severity & G_LOG_LEVEL_DEBUG)
    return "DEBUG";

  return "UNKNOWN";
}

static char *
format_time (GDateTime *capture_time,
             int64_t   start_time,
             int64_t   frame_time)
{
  g_autoptr (GDateTime) frame_date = NULL;
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

static void
print_message (GDateTime     *capture_time,
               int64_t       start_time,
               int64_t       frame_time,
               int32_t       pid,
               int16_t       cpu,
               const char   *severity,
               const char   *domain,
               const char   *message)
{
  g_autofree char *time = NULL;

  g_assert (severity != NULL);
  g_assert (domain != NULL);
  g_assert (message != NULL);

  time = format_time (capture_time, start_time, frame_time);
  g_print ("%s pid=%" G_GINT32_FORMAT " cpu=%" G_GINT16_FORMAT " %s %s: %s\n",
           time, pid, cpu, severity, domain, message);
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr (GDateTime) capture_time = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GOptionContext) context = NULL;
  g_autoptr (SysprofCaptureReader) reader = NULL;
  gboolean marks = FALSE;
  GOptionEntry entries[] = {
    { "marks", 0, 0, G_OPTION_ARG_NONE, &marks,
      "Include mark frames as log messages", NULL },
    { NULL }
  };
  SysprofCaptureFrameType type;
  int64_t start_time;

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

  start_time = sysprof_capture_reader_get_start_time (reader);
  capture_time = g_date_time_new_from_iso8601 (sysprof_capture_reader_get_time (reader), NULL);

  while (sysprof_capture_reader_peek_type (reader, &type))
    {
      if (type == SYSPROF_CAPTURE_FRAME_LOG)
        {
          const SysprofCaptureLog *log;

          if (!(log = sysprof_capture_reader_read_log (reader)))
            goto failure;

          print_message (capture_time,
                         start_time,
                         log->frame.time,
                         log->frame.pid,
                         log->frame.cpu,
                         severity_to_string (log->severity),
                         log->domain,
                         log->message);
        }
      else if (marks && type == SYSPROF_CAPTURE_FRAME_MARK)
        {
          const SysprofCaptureMark *mark;
          g_autofree char *domain = NULL;

          if (!(mark = sysprof_capture_reader_read_mark (reader)))
            goto failure;

          domain = g_strconcat (mark->group, "/", mark->name, NULL);
          print_message (capture_time,
                         start_time,
                         mark->frame.time,
                         mark->frame.pid,
                         mark->frame.cpu,
                         "MARK",
                         domain,
                         mark->message);
        }
      else if (!sysprof_capture_reader_skip (reader))
        {
          goto failure;
        }
    }

  return EXIT_SUCCESS;

failure:
  g_printerr ("Failed to read capture \"%s\".\n", argv[1]);
  return EXIT_FAILURE;
}
