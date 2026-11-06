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
#include "sysprof-memory-usage-private.h"
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

static const char *counter_names[] = {
  "Used",
  "Total",
  "Available",
  "Free",
  "Buffers",
  "Cached",
  "Swap Cached",
  "Mapped",
  "Shmem",
  "Swap Total",
  "Swap Free",
  "AnonPages",
  "Slab",
  "SReclaimable",
  "SUnreclaim",
  "Dirty",
  "Writeback",
};

#define N_MEMORY_COUNTERS SYSPROF_MEMORY_USAGE_N_VALUES

G_STATIC_ASSERT (G_N_ELEMENTS (counter_names) == N_MEMORY_COUNTERS);

typedef struct _MemStat
{
  int stat_fd;

  union {
    struct {
      gint64 used;
      gint64 total;
      gint64 avail;
      gint64 free;
      gint64 buffers;
      gint64 cached;
      gint64 swap_cached;
      gint64 mapped;
      gint64 shmem;
      gint64 swap_total;
      gint64 swap_free;
      gint64 anon_pages;
      gint64 slab;
      gint64 sreclaimable;
      gint64 sunreclaim;
      gint64 dirty;
      gint64 writeback;
    } sys;
    SysprofCaptureCounterValue values[N_MEMORY_COUNTERS];
  };
  gboolean present[N_MEMORY_COUNTERS];
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
  char *line = buf;

  g_assert (st != NULL);
  g_assert (buf != NULL);

  memset (st->present, 0, sizeof st->present);
  memset (st->values, 0, sizeof st->values);

  while (*line != 0)
    {
      char *end_line = strchr (line, '\n');
      char *colon;
      char *value;
      char *end_value;
      guint index;
      gint64 multiplier = 1;
      gint64 number;

      if (end_line != NULL)
        *end_line = 0;

      if ((colon = strchr (line, ':')) != NULL)
        {
          *colon = 0;
          index = GPOINTER_TO_UINT (g_hash_table_lookup (keys, line));
          value = colon + 1;

          while (g_ascii_isspace (*value))
            value++;

          errno = 0;
          number = g_ascii_strtoll (value, &end_value, 10);

          if (index != 0 && errno == 0 && end_value != value && number >= 0)
            {
              while (g_ascii_isspace (*end_value))
                end_value++;

              if (strcmp (end_value, "kB") == 0)
                multiplier = 1024;
              else if (strcmp (end_value, "mB") == 0)
                multiplier = 1024 * 1024;
              else if (strcmp (end_value, "gB") == 0)
                multiplier = G_GINT64_CONSTANT (1024) * 1024 * 1024;
              else if (*end_value != 0)
                multiplier = 0;

              if (multiplier != 0 && number <= G_MAXINT64 / multiplier)
                {
                  st->values[index - 1].v64 = number * multiplier;
                  st->present[index - 1] = TRUE;
                }
            }
        }

      if (end_line == NULL)
        break;

      line = end_line + 1;
    }

  if (st->present[1] && st->present[2] && st->sys.total >= st->sys.avail)
    {
      st->sys.used = st->sys.total - st->sys.avail;
      st->present[0] = TRUE;
    }
}

void
_sysprof_memory_usage_parse_meminfo (const char *contents,
                                     gint64     *values,
                                     gboolean   *present)
{
  g_autofree char *copy = NULL;
  MemStat st = { 0 };

  g_return_if_fail (contents != NULL);
  g_return_if_fail (values != NULL);
  g_return_if_fail (present != NULL);

  (void)g_type_class_get (SYSPROF_TYPE_MEMORY_USAGE);

  copy = g_strdup (contents);
  mem_stat_parse (&st, copy);

  for (guint i = 0; i < N_MEMORY_COUNTERS; i++)
    {
      values[i] = st.values[i].v64;
      present[i] = st.present[i];
    }
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
  SysprofCaptureCounter counters[N_MEMORY_COUNTERS] = { 0 };
  MemStat st;
  guint counter_base;

  g_assert (record != NULL);
  g_assert (SYSPROF_IS_RECORDING (record->recording));
  g_assert (DEX_IS_FUTURE (record->cancellable));

  buf = g_byte_array_new ();
  g_byte_array_set_size (buf, 16384);

  writer = _sysprof_recording_writer (record->recording);

  if (!mem_stat_open (&st, &error))
    return dex_future_new_for_error (g_steal_pointer (&error));

  counter_base = sysprof_capture_writer_request_counter (writer, N_MEMORY_COUNTERS);

  for (guint i = 0; i < N_MEMORY_COUNTERS; i++)
    {
      g_strlcpy (counters[i].category, "Memory", sizeof counters[i].category);
      g_strlcpy (counters[i].name, counter_names[i], sizeof counters[i].name);
      g_strlcpy (counters[i].description, "", sizeof counters[0].description);
      counters[i].id = counter_base + i;
      counters[i].type = SYSPROF_CAPTURE_COUNTER_INT64;
      counters[i].value.v64 = 0;
    }

  sysprof_capture_writer_define_counters (writer,
                                          SYSPROF_CAPTURE_CURRENT_TIME,
                                          -1,
                                          -1,
                                          counters,
                                          N_MEMORY_COUNTERS);

  for (;;)
    {
      g_autoptr(DexFuture) read_future = dex_aio_read (NULL, st.stat_fd, buf->data, buf->len-1, 0);
      gssize n_read;
      guint ids[N_MEMORY_COUNTERS];
      SysprofCaptureCounterValue values[N_MEMORY_COUNTERS];
      guint count = 0;
      gint64 time;

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

          time = SYSPROF_CAPTURE_CURRENT_TIME;

          for (guint i = 0; i < N_MEMORY_COUNTERS; i++)
            {
              if (st.present[i])
                {
                  ids[count] = counter_base + i;
                  values[count++] = st.values[i];
                }
            }

          if (count > 0)
            sysprof_capture_writer_set_counters (writer, time, -1, -1,
                                                 ids, values, count);
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

#define ADD_INDEX(n,i) g_hash_table_insert (keys, (gpointer)n, GUINT_TO_POINTER ((i) + 1))
  ADD_INDEX ("MemTotal", 1);
  ADD_INDEX ("MemAvailable", 2);
  ADD_INDEX ("MemFree", 3);
  ADD_INDEX ("Buffers", 4);
  ADD_INDEX ("Cached", 5);
  ADD_INDEX ("SwapCached", 6);
  ADD_INDEX ("Mapped", 7);
  ADD_INDEX ("Shmem", 8);
  ADD_INDEX ("SwapTotal", 9);
  ADD_INDEX ("SwapFree", 10);
  ADD_INDEX ("AnonPages", 11);
  ADD_INDEX ("Slab", 12);
  ADD_INDEX ("SReclaimable", 13);
  ADD_INDEX ("SUnreclaim", 14);
  ADD_INDEX ("Dirty", 15);
  ADD_INDEX ("Writeback", 16);
#undef ADD_INDEX
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
