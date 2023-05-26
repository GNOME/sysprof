/* sysprof-recording.c
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

#include <libdex.h>

#include "sysprof-instrument-private.h"
#include "sysprof-polkit-private.h"
#include "sysprof-recording-private.h"

typedef enum _SysprofRecordingCommand
{
  SYSPROF_RECORDING_COMMAND_STOP = 1,
} SysprofRecordingCommand;

struct _SysprofRecording
{
  GObject parent_instance;

  /* If we are spawning a process as part of this recording, this
   * is the SysprofSpawnable used to spawn the process.
   */
  SysprofSpawnable *spawnable;

  /* This is where all of the instruments will write to. They are
   * expected to do this from the main-thread only. To work from
   * additional threads they need to proxy that state to the
   * main thread for writing.
   */
  SysprofCaptureWriter *writer;

  /* An array of SysprofInstrument that are part of this recording */
  GPtrArray *instruments;

  /* A DexFiber that will complete when the recording has finished,
   * been stopped, or failed.
   */
  DexFuture *fiber;

  /* The channel is used ot send state change messages to the fiber
   * from outside of the fiber.
   */
  DexChannel *channel;
};

enum {
  PROP_0,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofRecording, sysprof_recording, G_TYPE_OBJECT)

static DexFuture *
sysprof_recording_fiber (gpointer user_data)
{
  SysprofRecording *self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (SYSPROF_IS_RECORDING (self));

  /* First ensure that all our required policy have been acquired on
   * the bus so that we don't need to individually acquire them from
   * each of the instruments after the recording starts.
   */
  if (!dex_await (_sysprof_instruments_acquire_policy (self->instruments, self), &error))
    return dex_future_new_for_error (g_steal_pointer (&error));

  /* Now allow instruments to prepare for the recording */
  if (!dex_await (_sysprof_instruments_prepare (self->instruments, self), &error))
    return dex_future_new_for_error (g_steal_pointer (&error));

  /* Wait for messages on our channel until closed. */
  for (;;)
    {
      SysprofRecordingCommand command;

      command = dex_await_uint (dex_channel_receive (self->channel), &error);

      switch (command)
        {
        default:
        case SYSPROF_RECORDING_COMMAND_STOP:
          goto stop_recording;
        }
    }

stop_recording:

  return dex_future_new_for_boolean (TRUE);
}

static void
sysprof_recording_finalize (GObject *object)
{
  SysprofRecording *self = (SysprofRecording *)object;

  if (self->channel)
    {
      dex_channel_close_send (self->channel);
      dex_clear (&self->channel);
    }

  g_clear_pointer (&self->writer, sysprof_capture_writer_unref);
  g_clear_pointer (&self->instruments, g_ptr_array_unref);
  dex_clear (&self->fiber);

  G_OBJECT_CLASS (sysprof_recording_parent_class)->finalize (object);
}

static void
sysprof_recording_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_recording_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_recording_class_init (SysprofRecordingClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_recording_finalize;
  object_class->get_property = sysprof_recording_get_property;
  object_class->set_property = sysprof_recording_set_property;
}

static void
sysprof_recording_init (SysprofRecording *self)
{
  self->channel = dex_channel_new (0);
  self->instruments = g_ptr_array_new_with_free_func (g_object_unref);
}

SysprofRecording *
_sysprof_recording_new (SysprofCaptureWriter  *writer,
                        SysprofInstrument    **instruments,
                        guint                  n_instruments)
{
  SysprofRecording *self;

  g_return_val_if_fail (writer != NULL, NULL);

  self = g_object_new (SYSPROF_TYPE_RECORDING, NULL);
  self->writer = sysprof_capture_writer_ref (writer);

  for (guint i = 0; i < n_instruments; i++)
    g_ptr_array_add (self->instruments, g_object_ref (instruments[i]));

  return self;
}

void
_sysprof_recording_start (SysprofRecording *self)
{

  g_return_if_fail (SYSPROF_IS_RECORDING (self));
  g_return_if_fail (self->fiber == NULL);

  self->fiber = dex_scheduler_spawn (NULL, 0,
                                     sysprof_recording_fiber,
                                     g_object_ref (self),
                                     g_object_unref);
}

void
sysprof_recording_stop_async (SysprofRecording    *self,
                              GCancellable        *cancellable,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
  g_autoptr(DexAsyncResult) result = NULL;

  g_return_if_fail (SYSPROF_IS_RECORDING (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  result = dex_async_result_new (self, cancellable, callback, user_data);
  dex_async_result_await (result,
                          dex_channel_send (self->channel,
                                            dex_future_new_for_uint (SYSPROF_RECORDING_COMMAND_STOP)));
}

gboolean
sysprof_recording_stop_finish (SysprofRecording  *self,
                               GAsyncResult      *result,
                               GError           **error)
{
  g_return_val_if_fail (SYSPROF_IS_RECORDING (self), FALSE);
  g_return_val_if_fail (DEX_IS_ASYNC_RESULT (result), FALSE);

  return dex_async_result_propagate_boolean (DEX_ASYNC_RESULT (result), error);
}

void
sysprof_recording_wait_async (SysprofRecording    *self,
                              GCancellable        *cancellable,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
  g_autoptr(DexAsyncResult) result = NULL;

  g_return_if_fail (SYSPROF_IS_RECORDING (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  result = dex_async_result_new (self, cancellable, callback, user_data);
  dex_async_result_await (result, dex_ref (self->fiber));
}

gboolean
sysprof_recording_wait_finish (SysprofRecording  *self,
                               GAsyncResult      *result,
                               GError           **error)
{
  g_return_val_if_fail (SYSPROF_IS_RECORDING (self), FALSE);
  g_return_val_if_fail (DEX_IS_ASYNC_RESULT (result), FALSE);

  return dex_async_result_propagate_boolean (DEX_ASYNC_RESULT (result), error);
}

SysprofSpawnable *
_sysprof_recording_get_spawnable (SysprofRecording *self)
{
  g_return_val_if_fail (SYSPROF_IS_RECORDING (self), NULL);

  return self->spawnable;
}
