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

static DexFuture *
sysprof_battery_charge_record_fiber (gpointer user_data)
{
  G_GNUC_UNUSED SysprofCaptureWriter *writer;
  Record *record = user_data;

  g_assert (record != NULL);
  g_assert (SYSPROF_IS_RECORDING (record->recording));
  g_assert (DEX_IS_FUTURE (record->cancellable));

  writer = _sysprof_recording_writer (record->recording);

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
