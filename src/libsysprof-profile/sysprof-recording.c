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

#include "sysprof-recording-private.h"

typedef enum _SysprofRecordingState
{
  SYSPROF_RECORDING_STATE_INITIAL,
  SYSPROF_RECORDING_STATE_PRE,
  SYSPROF_RECORDING_STATE_RECORD,
  SYSPROF_RECORDING_STATE_POST,
  SYSPROF_RECORDING_STATE_FINISHED,
  SYSPROF_RECORDING_STATE_ERROR,
} SysprofRecordingState;

struct _SysprofRecording
{
  GObject parent_instance;

  /* This is where all of the instruments will write to. They are
   * expected to do this from the main-thread only. To work from
   * additional threads they need to proxy that state to the
   * main thread for writing.
   */
  SysprofCaptureWriter *writer;

  /* Waiters contains a list of GTask to complete calls to the
   * sysprof_recording_wait_async() flow.
   */
  GQueue waiters;

  /* Our current state of operation */
  SysprofRecordingState state : 3;
};

enum {
  PROP_0,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofRecording, sysprof_recording, G_TYPE_OBJECT)

static gboolean
sysprof_recording_state_is_terminal (SysprofRecordingState state)
{
  return state == SYSPROF_RECORDING_STATE_ERROR ||
         state == SYSPROF_RECORDING_STATE_FINISHED;
}

static void
sysprof_recording_set_state (SysprofRecording      *self,
                             SysprofRecordingState  state)
{
  GQueue waiters;
  GTask *task;

  g_assert (SYSPROF_IS_RECORDING (self));

  self->state = state;

  if (!sysprof_recording_state_is_terminal (state))
    return;

  waiters = self->waiters;
  self->waiters = (GQueue) {NULL, NULL, 0};

  while ((task = g_queue_pop_head (&waiters)))
    {
      if (state == SYSPROF_RECORDING_STATE_ERROR)
        g_task_return_new_error (task,
                                 G_IO_ERROR,
                                 G_IO_ERROR_FAILED,
                                 "Recording failed");
      else
        g_task_return_boolean (task, TRUE);

      g_object_unref (task);
    }
}

static void
sysprof_recording_finalize (GObject *object)
{
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
  sysprof_recording_set_state (self, SYSPROF_RECORDING_STATE_INITIAL);
}

SysprofRecording *
_sysprof_recording_new (SysprofCaptureWriter *writer)
{
  SysprofRecording *self;

  g_return_val_if_fail (writer != NULL, NULL);

  self = g_object_new (SYSPROF_TYPE_RECORDING, NULL);
  self->writer = sysprof_capture_writer_ref (writer);

  return self;
}

void
sysprof_recording_stop (SysprofRecording *self)
{
  g_return_if_fail (SYSPROF_IS_RECORDING (self));

}

void
sysprof_recording_wait_async (SysprofRecording    *self,
                              GCancellable        *cancellable,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (SYSPROF_IS_RECORDING (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_recording_wait_async);

  switch (self->state)
    {
    case SYSPROF_RECORDING_STATE_INITIAL:
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_INVAL,
                               "Recording has not yet started");
      break;

    case SYSPROF_RECORDING_STATE_ERROR:
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_FAILED,
                               "Recording failed");
      break;

    case SYSPROF_RECORDING_STATE_FINISHED:
      g_task_return_boolean (task, TRUE);
      break;

    case SYSPROF_RECORDING_STATE_PRE:
    case SYSPROF_RECORDING_STATE_RECORD:
    case SYSPROF_RECORDING_STATE_POST:
      g_queue_push_tail (&self->waiters, g_steal_pointer (&task));
      break;

    default:
      g_assert_not_reached ();
    }
}

gboolean
sysprof_recording_wait_finish (SysprofRecording  *self,
                               GAsyncResult      *result,
                               GError           **error)
{
  g_return_val_if_fail (SYSPROF_IS_RECORDING (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);
  g_return_val_if_fail (g_task_is_valid (result, self), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}
