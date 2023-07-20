/* sysprof-memory-usage.c
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

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib-unix.h>
#include <glib/gstdio.h>

#include "line-reader-private.h"

#include "sysprof-memory-usage.h"
#include "sysprof-instrument-private.h"
#include "sysprof-recording-private.h"

struct _SysprofMemoryUsage
{
  SysprofInstrument parent_instance;
};

struct _SysprofMemoryUsageClass
{
  SysprofInstrumentClass parent_class;
};

typedef struct _MemStat
{
  int stat_fd;

  union {
    struct {
      SysprofCaptureCounterValue used;
      gint64 total;
      gint64 avail;
      gint64 free;
    } sys;
  };
} MemStat;

G_DEFINE_FINAL_TYPE (SysprofMemoryUsage, sysprof_memory_usage, SYSPROF_TYPE_INSTRUMENT)

static GHashTable *keys;

static gboolean
mem_stat_open (MemStat  *st,
               GError  **error)
{
  memset (st, 0, sizeof *st);

  st->stat_fd = open ("/proc/meminfo", O_RDONLY|O_CLOEXEC);

  if (st->stat_fd == -1)
    {
      int errsv = errno;
      g_set_error_literal (error,
                           G_IO_ERROR,
                           g_io_error_from_errno (errsv),
                           g_strerror (errsv));
      return FALSE;
    }

  return TRUE;
}

static gboolean
mem_stat_close (MemStat  *st,
                GError  **error)
{
  g_assert (st != NULL);

  return g_clear_fd (&st->stat_fd, error);
}

static void
mem_stat_parse (MemStat *st,
                char    *buf)
{
  char *bufptr = buf;
  char *save = NULL;

  g_assert (st != NULL);
  g_assert (buf != NULL);

  for (;;)
    {
      goffset off;
      char *key;
      char *value;
      char *unit;
      gint64 v64;
      gint64 *v64ptr;

      /* Get the data key name */
      if (!(key = strtok_r (bufptr, " \n\t:", &save)))
        break;

      bufptr = NULL;

      /* Offset from self to save value. Stop after getting to
       * last value we care about.
       */
      if (!(off = GPOINTER_TO_UINT (g_hash_table_lookup (keys, key))))
        break;

      /* Get the data value */
      if (!(value = strtok_r (bufptr, " \n\t:", &save)))
        break;

      /* Parse the numeric value of this column */
      v64 = g_ascii_strtoll (value, NULL, 10);
      if ((v64 == G_MININT64 || v64 == G_MAXINT64) && errno == ERANGE)
        break;

      /* Get the data unit */
      unit = strtok_r (bufptr, " \n\t:", &save);

      if (g_strcmp0 (unit, "kB") == 0)
        v64 *= 1024;
      else if (g_strcmp0 (unit, "mB") == 0)
        v64 *= 1024 * 1024;

      v64ptr = (gint64 *)(gpointer)(((gchar *)st) + off);

      *v64ptr = v64;
    }

  /* Create pre-compiled value for used to simplify display */
  st->sys.used.vdbl = (gdouble)st->sys.total - (gdouble)st->sys.avail;
}

typedef struct _Record
{
  SysprofRecording *recording;
  DexFuture        *cancellable;
} Record;

static void
record_free (gpointer data)
{
  Record *record = data;

  g_clear_object (&record->recording);
  dex_clear (&record->cancellable);
  g_free (record);
}

static DexFuture *
sysprof_memory_usage_record_fiber (gpointer user_data)
{
  g_autoptr(GByteArray) buf = NULL;
  Record *record = user_data;
  SysprofCaptureWriter *writer;
  g_autoptr(GError) error = NULL;
  SysprofCaptureCounter counters[1];
  MemStat st;
  guint counter_id;

  g_assert (record != NULL);
  g_assert (SYSPROF_IS_RECORDING (record->recording));
  g_assert (DEX_IS_FUTURE (record->cancellable));

  buf = g_byte_array_new ();
  g_byte_array_set_size (buf, 4096);

  writer = _sysprof_recording_writer (record->recording);

  if (!mem_stat_open (&st, &error))
    return dex_future_new_for_error (g_steal_pointer (&error));

  counter_id = sysprof_capture_writer_request_counter (writer, 1);

  g_strlcpy (counters[0].category, "Memory", sizeof counters[0].category);
  g_strlcpy (counters[0].name, "Used", sizeof counters[0].name);
  g_strlcpy (counters[0].description, "Memory used by system", sizeof counters[0].description);

  counters[0].id = counter_id;
  counters[0].type = SYSPROF_CAPTURE_COUNTER_DOUBLE;
  counters[0].value.vdbl = 0;

  sysprof_capture_writer_define_counters (writer,
                                          SYSPROF_CAPTURE_CURRENT_TIME,
                                          -1,
                                          -1,
                                          counters,
                                          1);

  for (;;)
    {
      g_autoptr(DexFuture) read_future = dex_aio_read (NULL, st.stat_fd, buf->data, buf->len-1, 0);
      gssize n_read;

      dex_await (dex_future_first (dex_ref (read_future),
                                   dex_ref (record->cancellable),
                                   NULL),
                 NULL);

      if (dex_future_get_status (read_future) != DEX_FUTURE_STATUS_RESOLVED)
        break;

      n_read = dex_await_int64 (dex_ref (read_future), NULL);
      if (n_read <= 0)
        break;

      if (n_read > 0)
        {
          buf->data[n_read] = 0;

          mem_stat_parse (&st, (char *)buf->data);

          sysprof_capture_writer_set_counters (writer,
                                               SYSPROF_CAPTURE_CURRENT_TIME,
                                               -1,
                                               -1,
                                               &counter_id,
                                               &st.sys.used,
                                               1);
        }

      dex_await (dex_future_first (dex_ref (record->cancellable),
                                   dex_timeout_new_usec (G_USEC_PER_SEC / 2),
                                   NULL),
                 NULL);

      if (dex_future_get_status (record->cancellable) == DEX_FUTURE_STATUS_REJECTED)
        break;
    }

  if (!mem_stat_close (&st, &error))
    return dex_future_new_for_error (g_steal_pointer (&error));

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
sysprof_memory_usage_record (SysprofInstrument *instrument,
                              SysprofRecording  *recording,
                              GCancellable      *cancellable)
{
  Record *record;

  g_assert (SYSPROF_IS_MEMORY_USAGE (instrument));
  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (G_IS_CANCELLABLE (cancellable));

  record = g_new0 (Record, 1);
  record->recording = g_object_ref (recording);
  record->cancellable = dex_cancellable_new_from_cancellable (cancellable);

  return dex_scheduler_spawn (NULL, 0,
                              sysprof_memory_usage_record_fiber,
                              record,
                              record_free);
}

static void
sysprof_memory_usage_class_init (SysprofMemoryUsageClass *klass)
{
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  instrument_class->record = sysprof_memory_usage_record;

  keys = g_hash_table_new (g_str_hash, g_str_equal);

#define ADD_OFFSET(n,o) \
  g_hash_table_insert (keys, (gchar *)n, GUINT_TO_POINTER (o))
  ADD_OFFSET ("MemTotal", G_STRUCT_OFFSET (MemStat, sys.total));
  ADD_OFFSET ("MemFree", G_STRUCT_OFFSET (MemStat, sys.free));
  ADD_OFFSET ("MemAvailable", G_STRUCT_OFFSET (MemStat, sys.avail));
#undef ADD_OFFSET
}

static void
sysprof_memory_usage_init (SysprofMemoryUsage *self)
{
}

SysprofInstrument *
sysprof_memory_usage_new (void)
{
  return g_object_new (SYSPROF_TYPE_MEMORY_USAGE, NULL);
}
