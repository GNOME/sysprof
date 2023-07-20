/* sysprof-tracer.c
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

#include <glib/gi18n.h>

#include "sysprof-instrument-private.h"
#include "sysprof-recording-private.h"
#include "sysprof-tracer.h"

/**
 * SysprofTracer:
 *
 * The `SysprofTracer` instrument provides integration with GCC's
 * `-finstrument-functions`. It uses an `LD_PRELOAD` on Linux to record
 * stacktraces on `__cyg_profile_func_enter` and `__cyg_profile_func_exit`.
 *
 * On macOS, `profile_func_enter` and `profile_func_exit` are used.
 */

struct _SysprofTracer
{
  SysprofInstrument parent_instance;
};

struct _SysprofTracerClass
{
  SysprofInstrumentClass parent_class;
};

G_DEFINE_FINAL_TYPE (SysprofTracer, sysprof_tracer, SYSPROF_TYPE_INSTRUMENT)

static DexFuture *
sysprof_tracer_prepare (SysprofInstrument *instrument,
                        SysprofRecording  *recording)
{
  SysprofSpawnable *spawnable;

  g_assert (SYSPROF_IS_INSTRUMENT (instrument));
  g_assert (SYSPROF_IS_RECORDING (recording));

  if ((spawnable = _sysprof_recording_get_spawnable (recording)))
    sysprof_spawnable_add_ld_preload (spawnable,
                                      PACKAGE_LIBDIR"/libsysprof-tracer-" API_VERSION_S ".so");
  else
    _sysprof_recording_diagnostic (recording,
                                   _("Tracer"),
                                   _("Tracing requires spawning a program compiled with ‘-finstrument-functions’. Tracing will not be available."));

  return dex_future_new_for_boolean (TRUE);
}

static void
sysprof_tracer_class_init (SysprofTracerClass *klass)
{
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  instrument_class->prepare = sysprof_tracer_prepare;
}

static void
sysprof_tracer_init (SysprofTracer *self)
{
}

SysprofInstrument *
sysprof_tracer_new (void)
{
  return g_object_new (SYSPROF_TYPE_TRACER, NULL);
}
