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
_sysprof_recording_spawn (SysprofSpawnable *spawnable)
{
  g_autoptr(GSubprocess) subprocess = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (SYSPROF_IS_SPAWNABLE (spawnable));

  if (!(subprocess = sysprof_spawnable_spawn (spawnable, &error)))
    return dex_future_new_for_error (g_steal_pointer (&error));

  return dex_subprocess_wait_check (subprocess);
}

static DexFuture *
sysprof_recording_fiber (gpointer user_data)
{
  SysprofRecording *self = user_data;
  g_autoptr(GCancellable) cancellable = NULL;
  g_autoptr(DexFuture) record = NULL;
  g_autoptr(DexFuture) monitor = NULL;
  g_autoptr(GError) error = NULL;
  gint64 begin_time;
  gint64 end_time;

  g_assert (SYSPROF_IS_RECORDING (self));

  cancellable = g_cancellable_new ();

  /* First ensure that all our required policy have been acquired on
   * the bus so that we don't need to individually acquire them from
   * each of the instruments after the recording starts.
   */
  if (!dex_await (_sysprof_instruments_acquire_policy (self->instruments, self), &error))
    return dex_future_new_for_error (g_steal_pointer (&error));

  /* Now allow instruments to prepare for the recording */
  if (!dex_await (_sysprof_instruments_prepare (self->instruments, self), &error))
    return dex_future_new_for_error (g_steal_pointer (&error));

  /* Ask instruments to start recording and stop if cancelled. */
  record = _sysprof_instruments_record (self->instruments, self, cancellable);

  /* Now take our begin time now that all instruments are notified */
  begin_time = SYSPROF_CAPTURE_CURRENT_TIME;

  /* If we need to spawn a subprocess, do it now */
  if (self->spawnable != NULL)
    monitor = _sysprof_recording_spawn (self->spawnable);
  else
    monitor = dex_future_new_infinite ();

  /* Wait for messages on our channel or the recording to complete */
  for (;;)
    {
      g_autoptr(DexFuture) message = dex_channel_receive (self->channel);

      /* Wait for either recording of all instruments to complete or a
       * message from our channel with what to do next.
       */
      if (!dex_await (dex_future_first (dex_ref (record),
                                        dex_ref (message),
                                        dex_ref (monitor),
                                        NULL),
                      &error))
        goto stop_recording;

      /* If record is not pending, then everything resolved/rejected */
      if (dex_future_get_status (record) != DEX_FUTURE_STATUS_PENDING ||
          dex_future_get_status (monitor) != DEX_FUTURE_STATUS_PENDING)
        goto stop_recording;

      /* If message resolved, then we got a command to process */
      if (dex_future_get_status (message) == DEX_FUTURE_STATUS_RESOLVED)
        {
          SysprofRecordingCommand command = dex_await_uint (dex_ref (message), NULL);

          switch (command)
            {
            default:
            case SYSPROF_RECORDING_COMMAND_STOP:
              goto stop_recording;
            }
        }
    }

stop_recording:
  end_time = SYSPROF_CAPTURE_CURRENT_TIME;

  /* Signal cancellable so that anything lingering has a chance to be
   * cleaned up, cascading into other subsystems.
   */
  g_cancellable_cancel (cancellable);

  /* Let instruments augment the capture. Some instruments may include
   * extra information about the capture such as symbol names and their
   * address ranges per-process.
   */
  dex_await (_sysprof_instruments_augment (self->instruments, self), NULL);

  /* Update start/end times to be the "running time" */
  _sysprof_capture_writer_set_time_range (self->writer, begin_time, end_time);

  if (error != NULL)
    return dex_future_new_for_error (g_steal_pointer (&error));

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
  g_clear_object (&self->spawnable);
  dex_clear (&self->fiber);

  G_OBJECT_CLASS (sysprof_recording_parent_class)->finalize (object);
}

static void
sysprof_recording_class_init (SysprofRecordingClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_recording_finalize;
}

static void
sysprof_recording_init (SysprofRecording *self)
{
  self->channel = dex_channel_new (0);
  self->instruments = g_ptr_array_new_with_free_func (g_object_unref);
}

SysprofRecording *
_sysprof_recording_new (SysprofCaptureWriter  *writer,
                        SysprofSpawnable      *spawnable,
                        SysprofInstrument    **instruments,
                        guint                  n_instruments)
{
  SysprofRecording *self;

  g_return_val_if_fail (writer != NULL, NULL);

  self = g_object_new (SYSPROF_TYPE_RECORDING, NULL);
  self->writer = sysprof_capture_writer_ref (writer);

  g_set_object (&self->spawnable, spawnable);

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

SysprofCaptureWriter *
_sysprof_recording_writer (SysprofRecording *self)
{
  g_return_val_if_fail (SYSPROF_IS_RECORDING (self), NULL);

  return self->writer;
}

typedef struct _AddFile
{
  SysprofCaptureWriter *writer;
  char *path;
  guint compress : 1;
} AddFile;

static void
add_file_free (AddFile *add_file)
{
  g_clear_pointer (&add_file->writer, sysprof_capture_writer_unref);
  g_clear_pointer (&add_file->path, g_free);
  g_free (add_file);
}

static DexFuture *
sysprof_recording_add_file_fiber (gpointer user_data)
{
  AddFile *add_file = user_data;
  g_autoptr(GInputStream) input = NULL;
  g_autoptr(GOutputStream) memory_stream = NULL;
  g_autoptr(GOutputStream) zlib_stream = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GBytes) bytes = NULL;
  g_autoptr(GFile) proc = NULL;
  g_autoptr(GFile) file = NULL;
  g_autofree char *dest_path = NULL;
  const guint8 *data = NULL;
  GOutputStream *output;
  gsize len;

  g_assert (add_file != NULL);
  g_assert (add_file->path != NULL);
  g_assert (add_file->writer != NULL);

  /* If we are compressing the file, store it as ".gz" in the capture
   * so that the reader knows to decompress it automatically.
   */
  dest_path = add_file->compress ? g_strdup_printf ("%s.gz", add_file->path)
                                 : g_strdup (add_file->path);

  file = g_file_new_for_path (add_file->path);
  proc = g_file_new_for_path ("/proc");

  /* If the file has a prefix of `/proc/` then we need to request the
   * file from sysprofd as our user is not guaranteed to be able to
   * read the file. Even if we can open the file, we may get data that
   * has been redacted (such as /proc/kallsyms).
   *
   * With `/proc/kallsyms` it's even worse in that if we get an FD back
   * from sysprofd and read it from our process, it *too* will be redacted.
   * That leaves us with the only option of letting sysprofd read the file
   * and transfer the contents to our process over D-Bus.
   *
   * We use g_file_has_prefix() for this as it will canonicalize the paths
   * of #GFile rather than us having to be careful here.
   */
  if (g_file_has_prefix (file, proc))
    {
      g_autoptr(GDBusConnection) connection = NULL;
      g_autoptr(GVariant) reply = NULL;
      g_autoptr(GBytes) input_bytes = NULL;

      if (!(connection = dex_await_object (dex_bus_get (G_BUS_TYPE_SYSTEM), &error)))
        return dex_future_new_for_error (g_steal_pointer (&error));

      if (!(reply = dex_await_variant (dex_dbus_connection_call (connection,
                                                                 "org.gnome.Sysprof3",
                                                                 "/org/gnome/Sysprof3",
                                                                 "org.gnome.Sysprof3.Service",
                                                                 "GetProcFile",
                                                                 g_variant_new ("(^ay)", g_file_get_path (file)),
                                                                 G_VARIANT_TYPE ("(ay)"),
                                                                 G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION,
                                                                 G_MAXINT),
                                       &error)))
        return dex_future_new_for_error (g_steal_pointer (&error));

      input_bytes = g_variant_get_data_as_bytes (reply);
      input = g_memory_input_stream_new_from_bytes (input_bytes);
    }
  else
    {
      if (!(input = dex_await_object (dex_file_read (file, 0), &error)))
        return dex_future_new_for_error (g_steal_pointer (&error));
    }

  g_assert (input != NULL);
  g_assert (G_IS_INPUT_STREAM (input));

  output = memory_stream = g_memory_output_stream_new_resizable ();

  if (add_file->compress)
    {
      g_autoptr(GZlibCompressor) compressor = g_zlib_compressor_new (G_ZLIB_COMPRESSOR_FORMAT_GZIP, 6);
      output = zlib_stream = g_converter_output_stream_new (memory_stream, G_CONVERTER (compressor));
    }

  g_assert (output != NULL);
  g_assert (G_IS_OUTPUT_STREAM (output));
  g_assert (G_IS_MEMORY_OUTPUT_STREAM (output) || G_IS_CONVERTER_OUTPUT_STREAM (output));
  g_assert (G_IS_MEMORY_OUTPUT_STREAM (memory_stream));
  g_assert (!zlib_stream || G_IS_CONVERTER_OUTPUT_STREAM (zlib_stream));

  if (!dex_await (dex_output_stream_splice (output,
                                            input,
                                            (G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE |
                                             G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET),
                                            0),
                  &error))
    return dex_future_new_for_error (g_steal_pointer (&error));

  bytes = g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (memory_stream));
  data = g_bytes_get_data (bytes, &len);

  while (len > 0)
    {
      gsize to_write = MIN (len, ((4096*8)-sizeof (SysprofCaptureFileChunk)));

      if (!sysprof_capture_writer_add_file (add_file->writer,
                                            SYSPROF_CAPTURE_CURRENT_TIME,
                                            -1,
                                            -1,
                                            dest_path,
                                            to_write == len,
                                            data,
                                            to_write))
        break;

      len -= to_write;
      data += to_write;
    }

  return  dex_future_new_for_boolean (TRUE);
}

DexFuture *
_sysprof_recording_add_file (SysprofRecording *self,
                             const char       *path,
                             gboolean          compress)
{
  g_autoptr(GFile) file = NULL;
  AddFile *add_file;

  g_return_val_if_fail (SYSPROF_IS_RECORDING (self), NULL);
  g_return_val_if_fail (path != NULL, NULL);

  add_file = g_new0 (AddFile, 1);
  add_file->writer = sysprof_capture_writer_ref (self->writer);
  add_file->path = g_strdup (path);
  add_file->compress = !!compress;

  return dex_scheduler_spawn (NULL, 0,
                              sysprof_recording_add_file_fiber,
                              g_steal_pointer (&add_file),
                              (GDestroyNotify)add_file_free);
}

void
_sysprof_recording_add_file_data (SysprofRecording *self,
                                  const char       *path,
                                  const char       *contents,
                                  gssize            length)
{
  g_return_if_fail (SYSPROF_IS_RECORDING (self));
  g_return_if_fail (path != NULL);
  g_return_if_fail (contents != NULL);

  if (length < 0)
    length = strlen (contents);

  while (length > 0)
    {
      gsize to_write = MIN (length, ((4096*8)-sizeof (SysprofCaptureFileChunk)));

      if (!sysprof_capture_writer_add_file (self->writer,
                                            SYSPROF_CAPTURE_CURRENT_TIME,
                                            -1,
                                            -1,
                                            path,
                                            to_write == length,
                                            (const guint8 *)contents,
                                            to_write))
        break;

      length -= to_write;
      contents += to_write;
    }
}
