/* sysprof-malloc-tracing.c
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

#include "sysprof-malloc-tracing.h"
#include "sysprof-instrument-private.h"
#include "sysprof-recording-private.h"
#include "sysprof-spawnable.h"

struct _SysprofMallocTracing
{
  SysprofInstrument parent_instance;
};

struct _SysprofMallocTracingClass
{
  SysprofInstrumentClass parent_class;
};

G_DEFINE_FINAL_TYPE (SysprofMallocTracing, sysprof_malloc_tracing, SYSPROF_TYPE_INSTRUMENT)

static DexFuture *
sysprof_malloc_tracing_prepare (SysprofInstrument *instrument,
                                SysprofRecording  *recording)
{
  SysprofSpawnable *spawnable;

  g_assert (SYSPROF_IS_MALLOC_TRACING (instrument));
  g_assert (SYSPROF_IS_RECORDING (recording));

  if ((spawnable = _sysprof_recording_get_spawnable (recording)))
    sysprof_spawnable_add_ld_preload (spawnable,
                                      PACKAGE_LIBDIR"/libsysprof-memory-" API_VERSION_S ".so");

  return dex_future_new_for_boolean (TRUE);
}

static void
sysprof_malloc_tracing_class_init (SysprofMallocTracingClass *klass)
{
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  instrument_class->prepare = sysprof_malloc_tracing_prepare;
}

static void
sysprof_malloc_tracing_init (SysprofMallocTracing *self)
{
}

SysprofInstrument *
sysprof_malloc_tracing_new (void)
{
  return g_object_new (SYSPROF_TYPE_MALLOC_TRACING, NULL);
}
