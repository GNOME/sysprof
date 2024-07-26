/* sysprof-battery-charge.c
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

#include "sysprof-battery-charge.h"
#include "sysprof-instrument-private.h"
#include "sysprof-recording-private.h"

#define SYS_CLASS_POWER_SUPPLY "/sys/class/power_supply/"

struct _SysprofBatteryCharge
{
  SysprofInstrument parent_instance;
};

struct _SysprofBatteryChargeClass
{
  SysprofInstrumentClass parent_class;
};

G_DEFINE_FINAL_TYPE (SysprofBatteryCharge, sysprof_battery_charge, SYSPROF_TYPE_INSTRUMENT)

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

static void
clear_fd (gpointer ptr)
{
  int *fdptr = ptr;
  g_clear_fd (fdptr, NULL);
}

static char **
collect_battery_names (void)
{
  GPtrArray *ar = g_ptr_array_new ();
  g_autoptr(GDir) dir = NULL;

  if ((dir = g_dir_open (SYS_CLASS_POWER_SUPPLY, 0, NULL)))
    {
      const char *name;

      while ((name = g_dir_read_name (dir)))
        {
          if (strcmp (name, "AC") == 0)
            continue;

          g_ptr_array_add (ar, g_strdup (name));
        }
    }

  g_ptr_array_add (ar, NULL);

  return (char **)g_ptr_array_free (ar, FALSE);
}

typedef struct _ReadBuffer
{
  char buf[32];
} ReadBuffer;

static DexFuture *
sysprof_battery_charge_record_fiber (gpointer user_data)
{
  const int invalid_fd = -1;
  g_autofree guint *ids = NULL;
  g_autofree SysprofCaptureCounterValue *values = NULL;
  g_autofree SysprofCaptureCounter *counters = NULL;
  g_autofree ReadBuffer *bufs = NULL;
  SysprofCaptureWriter *writer;
  Record *record = user_data;
  g_autoptr(GArray) charge_fds = NULL;
  g_auto(GStrv) names = NULL;
  guint n_names;
  guint n_counters = 1;

  g_assert (record != NULL);
  g_assert (SYSPROF_IS_RECORDING (record->recording));
  g_assert (DEX_IS_FUTURE (record->cancellable));

  writer = _sysprof_recording_writer (record->recording);
  names = collect_battery_names ();
  n_names = g_strv_length (names);

  /* Use some stack space for our counters and values. */
  ids = g_new0 (guint, n_names + 1);
  counters = g_new0 (SysprofCaptureCounter, n_names + 1);
  values = g_new0 (SysprofCaptureCounterValue, n_names + 1);
  bufs = g_new0 (ReadBuffer, n_names + 1);

  /* Setup the combined counter which is the total charge of all of
   * the batteries we discover on the system.
   */
  counters[0].id = ids[0] = sysprof_capture_writer_request_counter (writer, 1);
  g_strlcpy (counters[0].category, "Battery Charge", sizeof counters[0].category);
  g_strlcpy (counters[0].name, "Combined", sizeof counters[0].name);
  g_snprintf (counters[0].description, sizeof counters[0].description, "Combined Battery Charge (µAh)");
  counters[0].type = SYSPROF_CAPTURE_COUNTER_INT64;
  counters[0].value.v64 = 0;

  /* Create array to store open FDs to the charge file. We'll do a
   * positioned read at 0 on these so we get new data on each subsequent
   * read via AIO. Set the first FD to invalid since that will map to the
   * position of the combined-counter.
   */
  charge_fds = g_array_new (FALSE, FALSE, sizeof (int));
  g_array_set_clear_func (charge_fds, clear_fd);
  g_array_append_val (charge_fds, invalid_fd);

  for (guint i = 0; names[i]; i++)
    {
      SysprofCaptureCounter *counter = &counters[n_counters];
      const char *name = names[i];
      g_autofree char *charge_path = g_build_filename (SYS_CLASS_POWER_SUPPLY, name, "charge_now", NULL);
      g_autofree char *model_name_path = g_build_filename (SYS_CLASS_POWER_SUPPLY, name, "model_name", NULL);
      g_autofree char *type_path = g_build_filename (SYS_CLASS_POWER_SUPPLY, name, "type", NULL);
      g_autofree char *model_name_data = NULL;
      g_autofree char *type_data = NULL;
      g_autofd int charge_fd = -1;

      if (!g_file_get_contents (type_path, &type_data, NULL, NULL) ||
          !g_str_has_prefix (type_data, "Battery") ||
          -1 == (charge_fd = open (charge_path, O_RDONLY|O_CLOEXEC)))
        continue;

      counter->id = ids[n_counters] = sysprof_capture_writer_request_counter (writer, 1);
      counter->type = SYSPROF_CAPTURE_COUNTER_INT64;
      g_strlcpy (counter->category, "Battery Charge", sizeof counter->category);
      if (g_file_get_contents (model_name_path, &model_name_data, NULL, NULL))
        g_strlcpy (counter->name, g_strstrip (model_name_data), sizeof counter->name);
      else
        g_strlcpy (counter->name, name, sizeof counter->name);
      g_snprintf (counter->description, sizeof counter->description, "%s (µAh)", counter->name);
      counter->value.v64 = 0;

      g_array_append_val (charge_fds, charge_fd);
      charge_fd = -1;

      n_counters++;
    }

  /* If we only have the combined counter, then just short-circuit and
   * don't record any counters. Otherwise the battery charge row might
   * show up in UI which we would want to omit.
   */
  if (n_counters == 1)
    return dex_future_new_for_boolean (TRUE);

  sysprof_capture_writer_define_counters (writer,
                                          SYSPROF_CAPTURE_CURRENT_TIME,
                                          -1,
                                          -1,
                                          counters,
                                          n_counters);

  /* Main recording loop */
  for (;;)
    {
      g_autoptr(GPtrArray) futures = g_ptr_array_new_with_free_func (dex_unref);

      /* Read the contents of all the charge buffers in a single
       * submission to the AIO layer.
       */
      g_ptr_array_add (futures, dex_future_new_for_boolean (TRUE));
      for (guint i = 1; i < charge_fds->len; i++)
        {
          int charge_fd = g_array_index (charge_fds, int, i);

          g_ptr_array_add (futures,
                           dex_aio_read (NULL,
                                         charge_fd,
                                         &bufs[i].buf,
                                         sizeof bufs[i].buf-1,
                                         0));
        }

      /* Now wait until all the reads complete */
      if (futures->len > 0)
        dex_await (dex_future_anyv ((DexFuture **)futures->pdata, futures->len), NULL);

      /* Parse the current charge values */
      values[0].v64 = 0;
      for (guint i = 1; i < charge_fds->len; i++)
        {
          gssize len = dex_await_int64 (dex_ref (g_ptr_array_index (futures, i)), NULL);
          guint64 v64;

          if (len <= 0)
            continue;

          errno = 0;
          bufs[i].buf[len] = 0;
          v64 = g_ascii_strtoull (bufs[i].buf, NULL, 10);
          if (v64 == G_MAXUINT64 || errno != 0)
            continue;

          values[i].v64 = v64;
          values[0].v64 += v64;
        }

      /* Deliver new values to capture */
      sysprof_capture_writer_set_counters (writer,
                                           SYSPROF_CAPTURE_CURRENT_TIME,
                                           -1,
                                           -1,
                                           ids,
                                           values,
                                           n_counters);

      /* Wait for cancellation or ½ second */
      dex_await (dex_future_first (dex_ref (record->cancellable),
                                   dex_timeout_new_usec (G_USEC_PER_SEC / 2),
                                   NULL),
                 NULL);

      /* If cancellable is rejected, then we're done recording */
      if (dex_future_get_status (record->cancellable) != DEX_FUTURE_STATUS_PENDING)
        break;
    }

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
sysprof_battery_charge_record (SysprofInstrument *instrument,
                              SysprofRecording  *recording,
                              GCancellable      *cancellable)
{
  Record *record;

  g_assert (SYSPROF_IS_BATTERY_CHARGE (instrument));
  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (G_IS_CANCELLABLE (cancellable));

  record = g_new0 (Record, 1);
  record->recording = g_object_ref (recording);
  record->cancellable = dex_cancellable_new_from_cancellable (cancellable);

  return dex_scheduler_spawn (NULL, 0,
                              sysprof_battery_charge_record_fiber,
                              record,
                              record_free);
}

static void
sysprof_battery_charge_class_init (SysprofBatteryChargeClass *klass)
{
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  instrument_class->record = sysprof_battery_charge_record;
}

static void
sysprof_battery_charge_init (SysprofBatteryCharge *self)
{
}

SysprofInstrument *
sysprof_battery_charge_new (void)
{
  return g_object_new (SYSPROF_TYPE_BATTERY_CHARGE, NULL);
}
