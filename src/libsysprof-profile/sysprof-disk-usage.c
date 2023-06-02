/* sysprof-disk-usage.c
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

#include "sysprof-disk-usage.h"
#include "sysprof-instrument-private.h"

struct _SysprofDiskUsage
{
  SysprofInstrument parent_instance;
};

struct _SysprofDiskUsageClass
{
  SysprofInstrumentClass parent_class;
};

G_DEFINE_FINAL_TYPE (SysprofDiskUsage, sysprof_disk_usage, SYSPROF_TYPE_INSTRUMENT)

static DexFuture *
sysprof_disk_usage_record (SysprofInstrument *instrument,
                           SysprofRecording  *recording,
                           GCancellable      *cancellable)
{
  g_assert (SYSPROF_IS_INSTRUMENT (instrument));
  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (G_IS_CANCELLABLE (cancellable));

  return dex_future_new_for_boolean (TRUE);
}

static void
sysprof_disk_usage_class_init (SysprofDiskUsageClass *klass)
{
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  instrument_class->record = sysprof_disk_usage_record;
}

static void
sysprof_disk_usage_init (SysprofDiskUsage *self)
{
}

SysprofInstrument *
sysprof_disk_usage_new (void)
{
  return g_object_new (SYSPROF_TYPE_DISK_USAGE, NULL);
}
