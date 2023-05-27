/* sysprof-linux-instrument.c
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

#include "sysprof-linux-instrument-private.h"
#include "sysprof-recording-private.h"

struct _SysprofLinuxInstrument
{
  SysprofInstrument parent_instance;
};

enum {
  PROP_0,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofLinuxInstrument, sysprof_linux_instrument, SYSPROF_TYPE_INSTRUMENT)

static gboolean
sysprof_linux_instrument_capture_file (SysprofCaptureWriter  *writer,
                                       const char            *path,
                                       gboolean               compress,
                                       GError               **error)
{
  g_autoptr(GBytes) bytes = NULL;
  g_autoptr(GFile) file = NULL;
  const guint8 *data;
  gsize len;

  g_assert (writer != NULL);
  g_assert (path != NULL);

  file = g_file_new_for_path (path);

  /* TODO: This needs to go through sysprofd instead */

  if (!(bytes = dex_await_boxed (dex_file_load_contents_bytes (file), error)))
    return FALSE;

  /* TODO: Handle compression of files */

  data = g_bytes_get_data (bytes, &len);

  while (len > 0)
    {
      gsize to_write = MIN (len, 4096*8);

      if (!sysprof_capture_writer_add_file (writer,
                                            SYSPROF_CAPTURE_CURRENT_TIME,
                                            -1,
                                            -1,
                                            path,
                                            to_write == len,
                                            data,
                                            to_write))
        break;

      len -= to_write;
      data += to_write;
    }

  return TRUE;
}

static DexFuture *
sysprof_linux_instrument_prepare_fiber (gpointer user_data)
{
  SysprofRecording *recording = user_data;
  SysprofCaptureWriter *writer;

  g_assert (SYSPROF_IS_RECORDING (recording));

  writer = _sysprof_recording_writer (recording);

  /* Capture information about the system as we see it from the host system.
   * We must query this via sysprofd so that our current process (which may
   * have a different mount namespace) does not fall into the capture file.
   */

  sysprof_linux_instrument_capture_file (writer, "/proc/cpuinfo", FALSE, NULL);
  sysprof_linux_instrument_capture_file (writer, "/proc/mounts", FALSE, NULL);
  sysprof_linux_instrument_capture_file (writer, "/proc/kallsyms", TRUE, NULL);

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
sysprof_linux_instrument_prepare (SysprofInstrument *instrument,
                                  SysprofRecording  *recording)
{
  g_assert (SYSPROF_IS_INSTRUMENT (instrument));
  g_assert (SYSPROF_IS_RECORDING (recording));

  return dex_scheduler_spawn (NULL, 0,
                              sysprof_linux_instrument_prepare_fiber,
                              g_object_ref (recording),
                              g_object_unref);
}

static void
sysprof_linux_instrument_finalize (GObject *object)
{
  G_OBJECT_CLASS (sysprof_linux_instrument_parent_class)->finalize (object);
}

static void
sysprof_linux_instrument_class_init (SysprofLinuxInstrumentClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  object_class->finalize = sysprof_linux_instrument_finalize;

  instrument_class->prepare = sysprof_linux_instrument_prepare;
}

static void
sysprof_linux_instrument_init (SysprofLinuxInstrument *self)
{
}

SysprofInstrument *
_sysprof_linux_instrument_new (void)
{
  return g_object_new (SYSPROF_TYPE_LINUX_INSTRUMENT, NULL);
}
