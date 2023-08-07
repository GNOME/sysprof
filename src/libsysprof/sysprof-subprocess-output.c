/* sysprof-subprocess-output.c
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

#include "sysprof-enums.h"
#include "sysprof-instrument-private.h"
#include "sysprof-recording-private.h"
#include "sysprof-subprocess-output.h"

struct _SysprofSubprocessOutput
{
  SysprofInstrument parent_instance;

  char *stdout_path;
  char *command_cwd;
  char **command_argv;
  char **command_environ;

  SysprofRecording *recording;
  GCancellable *cancellable;

  SysprofRecordingPhase phase;
};

struct _SysprofSubprocessOutputClass
{
  SysprofInstrumentClass parent_class;
};

enum {
  PROP_0,
  PROP_COMMAND_ARGV,
  PROP_COMMAND_ENVIRON,
  PROP_COMMAND_CWD,
  PROP_PHASE,
  PROP_STDOUT_PATH,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofSubprocessOutput, sysprof_subprocess_output, SYSPROF_TYPE_INSTRUMENT)

static GParamSpec *properties [N_PROPS];

static void
add_process_output_as_file_cb (GObject      *object,
                               GAsyncResult *result,
                               gpointer      user_data)
{
  g_autoptr(DexPromise) promise = user_data;
  g_autoptr(GError) error = NULL;
  g_autofree char *stdout_buf = NULL;

  g_assert (G_IS_SUBPROCESS (object));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (DEX_IS_PROMISE (promise));

  if (g_subprocess_communicate_utf8_finish (G_SUBPROCESS (object), result, &stdout_buf, NULL, &error))
    dex_promise_resolve_string (promise, g_steal_pointer (&stdout_buf));
  else
    dex_promise_reject (promise, g_steal_pointer (&error));
}

static void
add_process_output_as_file (SysprofRecording   *recording,
                            const char * const *argv,
                            const char * const *env,
                            const char         *filename,
                            gboolean            compress,
                            GCancellable       *cancellable)
{
  g_autoptr(GSubprocessLauncher) launcher = NULL;
  g_autoptr(GSubprocess) subprocess = NULL;
  g_autoptr(DexPromise) promise = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *string = NULL;

  g_assert (SYSPROF_IS_RECORDING (recording));

  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE);
  if (env != NULL)
    g_subprocess_launcher_set_environ (launcher, (char **)env);
  if (!(subprocess = g_subprocess_launcher_spawnv (launcher, argv, &error)))
    goto error;

  promise = dex_promise_new ();
  g_subprocess_communicate_utf8_async (subprocess,
                                       NULL,
                                       cancellable,
                                       add_process_output_as_file_cb,
                                       dex_ref (promise));

  if (!(string = dex_await_string (dex_ref (promise), &error)))
    goto error;

  _sysprof_recording_add_file_data (recording, filename, string, -1, compress);

  return;

error:
  _sysprof_recording_diagnostic (recording,
                                 "Subprocess",
                                 "Failed to get command output: %s",
                                 error->message);
}

static DexFuture *
sysprof_subprocess_output_record_fiber (gpointer user_data)
{
  SysprofSubprocessOutput *self = user_data;

  g_assert (SYSPROF_IS_SUBPROCESS_OUTPUT (self));

  if (self->command_argv && self->stdout_path)
    add_process_output_as_file (self->recording,
                                (const char * const *)self->command_argv,
                                (const char * const *)self->command_environ,
                                self->stdout_path,
                                TRUE,
                                self->cancellable);

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
sysprof_subprocess_output_record (SysprofInstrument *instrument,
                                  SysprofRecording  *recording,
                                  GCancellable      *cancellable)
{
  SysprofSubprocessOutput *self = (SysprofSubprocessOutput *)instrument;

  g_assert (SYSPROF_IS_SUBPROCESS_OUTPUT (self));
  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (G_IS_CANCELLABLE (cancellable));

  if (self->phase != SYSPROF_RECORDING_PHASE_RECORD)
    return dex_future_new_for_boolean (TRUE);

  g_set_object (&self->recording, recording);
  g_set_object (&self->cancellable, cancellable);

  return dex_scheduler_spawn (NULL, 0,
                              sysprof_subprocess_output_record_fiber,
                              g_object_ref (self),
                              g_object_unref);
}

static DexFuture *
sysprof_subprocess_output_prepare (SysprofInstrument *instrument,
                                   SysprofRecording  *recording)
{
  SysprofSubprocessOutput *self = (SysprofSubprocessOutput *)instrument;

  g_assert (SYSPROF_IS_SUBPROCESS_OUTPUT (self));
  g_assert (SYSPROF_IS_RECORDING (recording));

  if (self->phase != SYSPROF_RECORDING_PHASE_PREPARE)
    return dex_future_new_for_boolean (TRUE);

  g_set_object (&self->recording, recording);

  return dex_scheduler_spawn (NULL, 0,
                              sysprof_subprocess_output_record_fiber,
                              g_object_ref (self),
                              g_object_unref);
}

static DexFuture *
sysprof_subprocess_output_augment (SysprofInstrument *instrument,
                                   SysprofRecording  *recording)
{
  SysprofSubprocessOutput *self = (SysprofSubprocessOutput *)instrument;

  g_assert (SYSPROF_IS_SUBPROCESS_OUTPUT (self));
  g_assert (SYSPROF_IS_RECORDING (recording));

  if (self->phase != SYSPROF_RECORDING_PHASE_AUGMENT)
    return dex_future_new_for_boolean (TRUE);

  g_set_object (&self->recording, recording);

  return dex_scheduler_spawn (NULL, 0,
                              sysprof_subprocess_output_record_fiber,
                              g_object_ref (self),
                              g_object_unref);
}

static void
sysprof_subprocess_output_dispose (GObject *object)
{
  SysprofSubprocessOutput *self = (SysprofSubprocessOutput *)object;

  g_clear_pointer (&self->command_argv, g_strfreev);
  g_clear_pointer (&self->command_environ, g_strfreev);
  g_clear_pointer (&self->command_cwd, g_free);
  g_clear_pointer (&self->stdout_path, g_free);
  g_clear_object (&self->recording);
  g_clear_object (&self->cancellable);

  G_OBJECT_CLASS (sysprof_subprocess_output_parent_class)->dispose (object);
}

static void
sysprof_subprocess_output_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  SysprofSubprocessOutput *self = SYSPROF_SUBPROCESS_OUTPUT (object);

  switch (prop_id)
    {
    case PROP_COMMAND_CWD:
      g_value_set_string (value, sysprof_subprocess_output_get_command_cwd (self));
      break;

    case PROP_COMMAND_ARGV:
      g_value_set_boxed (value, sysprof_subprocess_output_get_command_argv (self));
      break;

    case PROP_COMMAND_ENVIRON:
      g_value_set_boxed (value, sysprof_subprocess_output_get_command_environ (self));
      break;

    case PROP_PHASE:
      g_value_set_enum (value, sysprof_subprocess_output_get_phase (self));
      break;

    case PROP_STDOUT_PATH:
      g_value_set_string (value, sysprof_subprocess_output_get_stdout_path (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_subprocess_output_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  SysprofSubprocessOutput *self = SYSPROF_SUBPROCESS_OUTPUT (object);

  switch (prop_id)
    {
    case PROP_COMMAND_CWD:
      sysprof_subprocess_output_set_command_cwd (self, g_value_get_string (value));
      break;

    case PROP_COMMAND_ARGV:
      sysprof_subprocess_output_set_command_argv (self, g_value_get_boxed (value));
      break;

    case PROP_COMMAND_ENVIRON:
      sysprof_subprocess_output_set_command_environ (self, g_value_get_boxed (value));
      break;

    case PROP_PHASE:
      sysprof_subprocess_output_set_phase (self, g_value_get_enum (value));
      break;

    case PROP_STDOUT_PATH:
      sysprof_subprocess_output_set_stdout_path (self, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_subprocess_output_class_init (SysprofSubprocessOutputClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  object_class->dispose = sysprof_subprocess_output_dispose;
  object_class->get_property = sysprof_subprocess_output_get_property;
  object_class->set_property = sysprof_subprocess_output_set_property;

  instrument_class->prepare = sysprof_subprocess_output_prepare;
  instrument_class->record = sysprof_subprocess_output_record;
  instrument_class->augment = sysprof_subprocess_output_augment;

  properties[PROP_COMMAND_CWD] =
    g_param_spec_string ("command-cwd", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_COMMAND_ARGV] =
    g_param_spec_boxed ("command-argv", NULL, NULL,
                        G_TYPE_STRV,
                        (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_COMMAND_ENVIRON] =
    g_param_spec_boxed ("command-environ", NULL, NULL,
                        G_TYPE_STRV,
                        (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_PHASE] =
    g_param_spec_enum ("phase", NULL, NULL,
                       SYSPROF_TYPE_RECORDING_PHASE,
                       SYSPROF_RECORDING_PHASE_PREPARE,
                       (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_STDOUT_PATH] =
    g_param_spec_string ("stdout-path", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_subprocess_output_init (SysprofSubprocessOutput *self)
{
  self->phase = SYSPROF_RECORDING_PHASE_PREPARE;
}

const char *
sysprof_subprocess_output_get_command_cwd (SysprofSubprocessOutput *self)
{
  g_return_val_if_fail (SYSPROF_IS_SUBPROCESS_OUTPUT (self), NULL);

  return self->command_cwd;
}

void
sysprof_subprocess_output_set_command_cwd (SysprofSubprocessOutput *self,
                                           const char              *command_cwd)
{
  g_return_if_fail (SYSPROF_IS_SUBPROCESS_OUTPUT (self));

  if (g_set_str (&self->command_cwd, command_cwd))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COMMAND_CWD]);
}

const char * const *
sysprof_subprocess_output_get_command_argv (SysprofSubprocessOutput *self)
{
  g_return_val_if_fail (SYSPROF_IS_SUBPROCESS_OUTPUT (self), NULL);

  return (const char * const *)self->command_argv;
}

void
sysprof_subprocess_output_set_command_argv (SysprofSubprocessOutput *self,
                                            const char * const      *command_argv)
{
  char **copy;

  g_return_if_fail (SYSPROF_IS_SUBPROCESS_OUTPUT (self));

  if (command_argv == (gpointer)self->command_argv ||
      (command_argv && self->command_argv &&
       g_strv_equal (command_argv, (const char * const *)self->command_argv)))
    return;

  copy = g_strdupv ((char **)command_argv);

  g_strfreev (self->command_argv);
  self->command_argv = copy;

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COMMAND_ARGV]);
}

const char * const *
sysprof_subprocess_output_get_command_environ (SysprofSubprocessOutput *self)
{
  g_return_val_if_fail (SYSPROF_IS_SUBPROCESS_OUTPUT (self), NULL);

  return (const char * const *)self->command_environ;
}

void
sysprof_subprocess_output_set_command_environ (SysprofSubprocessOutput *self,
                                               const char * const      *command_environ)
{
  char **copy;

  g_return_if_fail (SYSPROF_IS_SUBPROCESS_OUTPUT (self));

  if (command_environ == (gpointer)self->command_environ ||
      (command_environ && self->command_environ &&
       g_strv_equal (command_environ, (const char * const *)self->command_environ)))
    return;

  copy = g_strdupv ((char **)command_environ);

  g_strfreev (self->command_environ);
  self->command_environ = copy;

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COMMAND_ENVIRON]);
}

const char *
sysprof_subprocess_output_get_stdout_path (SysprofSubprocessOutput *self)
{
  g_return_val_if_fail (SYSPROF_IS_SUBPROCESS_OUTPUT (self), NULL);

  return self->stdout_path;
}

void
sysprof_subprocess_output_set_stdout_path (SysprofSubprocessOutput *self,
                                           const char              *stdout_path)
{
  g_return_if_fail (SYSPROF_IS_SUBPROCESS_OUTPUT (self));

  if (g_set_str (&self->stdout_path, stdout_path))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STDOUT_PATH]);
}

SysprofRecordingPhase
sysprof_subprocess_output_get_phase (SysprofSubprocessOutput *self)
{
  g_return_val_if_fail (SYSPROF_IS_SUBPROCESS_OUTPUT (self), 0);

  return self->phase;
}

void
sysprof_subprocess_output_set_phase (SysprofSubprocessOutput *self,
                                     SysprofRecordingPhase    phase)
{
  g_return_if_fail (phase > 0);
  g_return_if_fail (phase <= SYSPROF_RECORDING_PHASE_AUGMENT);

  if (phase == self->phase)
    return;

  self->phase = phase;
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PHASE]);
}
