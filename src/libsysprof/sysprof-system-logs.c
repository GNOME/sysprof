/* sysprof-system-logs.c
 *
 * Copyright 2023 Christian Hergert <chergert@redhat.com>
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
#include <glib/gstdio.h>

#include "sysprof-system-logs.h"
#include "sysprof-instrument-private.h"
#include "sysprof-recording-private.h"

#if HAVE_LIBSYSTEMD
# include "sysprof-journald-source.h"
#endif

struct _SysprofSystemLogs
{
  SysprofInstrument parent_instance;
};

struct _SysprofSystemLogsClass
{
  SysprofInstrumentClass parent_class;
};

G_DEFINE_FINAL_TYPE (SysprofSystemLogs, sysprof_system_logs, SYSPROF_TYPE_INSTRUMENT)

#if HAVE_LIBSYSTEMD
static char *
journal_get_data (sd_journal *journal,
                  const char *field)
{
  gconstpointer data = NULL;
  const char *endptr;
  const char *eq;
  gsize length;
  int ret;

  g_assert (journal != NULL);
  g_assert (field != NULL);

  if ((ret = sd_journal_get_data (journal, field, &data, &length)) < 0)
    return NULL;

  endptr = (const char *)data + length;
  if (!(eq = memchr (data, '=', length)))
    return NULL;

  eq++;

  return g_strndup (eq, endptr - eq);
}

static gboolean
sysprof_system_logs_callback (SysprofCaptureWriter *writer,
                              sd_journal           *journal)
{
  g_autofree char *message = NULL;
  g_autofree char *priority = NULL;
  sd_id128_t boot_id;
  guint64 usec;
  int severity = 0;

  g_assert (journal != NULL);
  g_assert (writer != NULL);

  if (sd_journal_get_monotonic_usec (journal, &usec, &boot_id) != 0 ||
      !(message = journal_get_data (journal, "MESSAGE")))
    return G_SOURCE_CONTINUE;

  priority = journal_get_data (journal, "PRIORITY");

  if (priority != NULL)
    {
      switch (priority[0])
        {
        case '0':
        case '1':
          severity = G_LOG_LEVEL_ERROR;
          break;

        case '2':
          severity = G_LOG_LEVEL_CRITICAL;
          break;

        case '3':
        case '4':
          severity = G_LOG_LEVEL_WARNING;
          break;

        default:
        case '5':
          severity = G_LOG_LEVEL_MESSAGE;
          break;

        case '6':
          severity = G_LOG_LEVEL_INFO;
          break;

        case '7':
          severity = G_LOG_LEVEL_DEBUG;
          break;
        }
    }

  sysprof_capture_writer_add_log (writer, usec*1000, -1, -1, severity, "System Log", message);

  return G_SOURCE_CONTINUE;
}
#endif

#if HAVE_LIBSYSTEMD
static DexFuture *
sysprof_system_logs_record_finished (DexFuture *future,
                                     gpointer   user_data)
{
  GSource *source = user_data;
  g_source_destroy (source);
  return dex_future_new_for_boolean (TRUE);
}
#endif

static DexFuture *
sysprof_system_logs_record (SysprofInstrument *instrument,
                            SysprofRecording  *recording,
                            GCancellable      *cancellable)
{
#if HAVE_LIBSYSTEMD
  SysprofCaptureWriter *writer;
  GSource *source;

  g_assert (SYSPROF_IS_SYSTEM_LOGS (instrument));
  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (G_IS_CANCELLABLE (cancellable));

  writer = _sysprof_recording_writer (recording);

  source = sysprof_journald_source_new ((SysprofJournaldSourceFunc)sysprof_system_logs_callback,
                                        sysprof_capture_writer_ref (writer),
                                        (GDestroyNotify)sysprof_capture_writer_unref);
  g_source_set_static_name (source, "[sysprof-journald]");
  g_source_set_priority (source, G_PRIORITY_LOW);
  g_source_attach (source, NULL);

  return dex_future_finally (dex_cancellable_new_from_cancellable (cancellable),
                             sysprof_system_logs_record_finished,
                             source,
                             (GDestroyNotify)g_source_unref);
#else
  _sysprof_recording_diagnostic (recording,
                                 _("System Logs"),
                                 _("Recording system logs is not supported on your platform."));
  return dex_future_new_for_boolean (TRUE);
#endif
}

static void
sysprof_system_logs_class_init (SysprofSystemLogsClass *klass)
{
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  instrument_class->record = sysprof_system_logs_record;
}

static void
sysprof_system_logs_init (SysprofSystemLogs *self)
{
}

SysprofInstrument *
sysprof_system_logs_new (void)
{
  return g_object_new (SYSPROF_TYPE_SYSTEM_LOGS, NULL);
}
