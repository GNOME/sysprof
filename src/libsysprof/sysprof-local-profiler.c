/* sysprof-local-profiler.c
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sysprof-local-profiler"

#include "config.h"

#include <errno.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <sysprof-capture.h>
#include <unistd.h>

#include "sysprof-helpers.h"
#include "sysprof-local-profiler.h"
#include "sysprof-platform.h"

#include "sysprof-capture-autocleanups.h"
#include "sysprof-control-source.h"
#include "sysprof-gjs-source.h"
#include "sysprof-hostinfo-source.h"
#ifdef __linux__
# include "sysprof-perf-source.h"
#endif
#include "sysprof-proc-source.h"
#include "sysprof-proxy-source.h"

#define CSTRV(s) ((const gchar * const *)s)

typedef struct
{
  SysprofCaptureWriter *writer;

  /* All sources added */
  GPtrArray *sources;

  /* Array of GError failures */
  GPtrArray *failures;

  /* Sources currently starting */
  GPtrArray *starting;

  /* Sources currently stopping */
  GPtrArray *stopping;

  /* Sources that have failed or finished */
  GPtrArray *finished_or_failed;

  /* Pids to notify children about before prepare */
  GArray *pids;

  /* Timer for simple time tracking */
  GTimer *timer;
  guint timer_notify_source;

  /* Arguments and environment variables for spawning */
  gchar **spawn_argv;
  gchar **spawn_env;
  gchar *spawn_cwd;

  /* State flags */
  guint is_running : 1;
  guint is_stopping : 1;
  guint is_starting : 1;

  /*
   * If we should spawn argv when starting up. This allows UI to set
   * spawn argv/env but enable disable with a toggle.
   */
  guint spawn : 1;

  /* If we should inherit the environment when spawning */
  guint spawn_inherit_environ : 1;

  /*
   * If we should profile the entire system. Setting this results in pids
   * being ignored. This is primarily useful for UI to toggle on/off the
   * feature of per-process vs whole-system.
   */
  guint whole_system : 1;

  /*
   * If we got a stop request after calling start() but before we have had
   * a chance to settle, then we need to stop immediately after starting.
   * We do this to avoid a more complex state machine (for now).
   */
  guint stop_after_starting : 1;
} SysprofLocalProfilerPrivate;

static void profiler_iface_init (SysprofProfilerInterface *iface);

G_DEFINE_TYPE_EXTENDED (SysprofLocalProfiler, sysprof_local_profiler, G_TYPE_OBJECT, 0,
                        G_ADD_PRIVATE (SysprofLocalProfiler)
                        G_IMPLEMENT_INTERFACE (SYSPROF_TYPE_PROFILER, profiler_iface_init))

enum {
  PROP_0,
  N_PROPS,

  PROP_ELAPSED,
  PROP_IS_MUTABLE,
  PROP_IS_RUNNING,
  PROP_SPAWN,
  PROP_SPAWN_ARGV,
  PROP_SPAWN_CWD,
  PROP_SPAWN_ENV,
  PROP_SPAWN_INHERIT_ENVIRON,
  PROP_WHOLE_SYSTEM,
};

static inline gint
_g_ptr_array_find (GPtrArray *ar,
                   gpointer   item)
{
  guint i;

  for (i = 0; i < ar->len; i++)
    {
      if (item == g_ptr_array_index (ar, i))
        return i;
    }

  return -1;
}

static inline gboolean
_g_ptr_array_contains (GPtrArray *ar,
                       gpointer   item)
{
  return (-1 != _g_ptr_array_find (ar, item));
}

static void
sysprof_local_profiler_clear_timer (SysprofLocalProfiler *self)
{
  SysprofLocalProfilerPrivate *priv = sysprof_local_profiler_get_instance_private (self);

  g_assert (SYSPROF_IS_LOCAL_PROFILER (self));

  g_clear_pointer (&priv->timer, g_timer_destroy);

  if (priv->timer_notify_source != 0)
    {
      g_source_remove (priv->timer_notify_source);
      priv->timer_notify_source = 0;
    }
}

static void
sysprof_local_profiler_real_stopped (SysprofProfiler *profiler)
{
  SysprofLocalProfiler *self = (SysprofLocalProfiler *)profiler;

  g_assert (SYSPROF_IS_LOCAL_PROFILER (self));

  sysprof_local_profiler_clear_timer (self);
}

static gboolean
sysprof_local_profiler_notify_elapsed_cb (gpointer data)
{
  SysprofLocalProfiler *self = data;

  g_assert (SYSPROF_IS_LOCAL_PROFILER (self));

  g_object_notify (G_OBJECT (self), "elapsed");

  return G_SOURCE_CONTINUE;
}

static void
sysprof_local_profiler_finish_stopping (SysprofLocalProfiler *self)
{
  SysprofLocalProfilerPrivate *priv = sysprof_local_profiler_get_instance_private (self);
  g_autoptr(SysprofCaptureReader) reader = NULL;

  g_assert (SYSPROF_IS_LOCAL_PROFILER (self));
  g_assert (priv->is_starting == FALSE);
  g_assert (priv->is_stopping == TRUE);
  g_assert (priv->stopping->len == 0);

  reader = sysprof_capture_writer_create_reader (priv->writer);
  g_assert (reader != NULL);

  for (guint i = 0; i < priv->sources->len; i++)
    {
      SysprofSource *source = g_ptr_array_index (priv->sources, i);

      sysprof_capture_reader_reset (reader);
      sysprof_source_supplement (source, reader);
    }

  if (priv->failures->len > 0)
    {
      const GError *error = g_ptr_array_index (priv->failures, 0);

      sysprof_profiler_emit_failed (SYSPROF_PROFILER (self), error);
    }

  priv->is_running = FALSE;
  priv->is_stopping = FALSE;

  sysprof_profiler_emit_stopped (SYSPROF_PROFILER (self));

  g_object_notify (G_OBJECT (self), "is-mutable");
  g_object_notify (G_OBJECT (self), "is-running");
}

static void
sysprof_local_profiler_stop (SysprofProfiler *profiler)
{
  SysprofLocalProfiler *self = (SysprofLocalProfiler *)profiler;
  SysprofLocalProfilerPrivate *priv = sysprof_local_profiler_get_instance_private (self);
  guint i;

  g_return_if_fail (SYSPROF_IS_LOCAL_PROFILER (self));

  if (priv->is_starting)
    {
      priv->stop_after_starting = TRUE;
      return;
    }

  if (priv->is_stopping || !priv->is_running)
    return;

  priv->is_stopping = TRUE;

  /*
   * First we add everything to the stopping list, so that we can
   * be notified of when they have completed. If everything stopped
   * synchronously, the stopping list will be empty after calling
   * sysprof_source_stop() for every source. Otherwise, we need to delay
   * stopping for a little bit.
   */

  for (i = 0; i < priv->sources->len; i++)
    {
      SysprofSource *source = g_ptr_array_index (priv->sources, i);

      if (!_g_ptr_array_contains (priv->finished_or_failed, source))
        g_ptr_array_add (priv->stopping, g_object_ref (source));
    }

  for (i = 0; i < priv->sources->len; i++)
    {
      SysprofSource *source = g_ptr_array_index (priv->sources, i);

      sysprof_source_stop (source);
    }

  if (priv->is_stopping && priv->stopping->len == 0)
    sysprof_local_profiler_finish_stopping (self);
}


static void
sysprof_local_profiler_dispose (GObject *object)
{
  SysprofLocalProfiler *self = (SysprofLocalProfiler *)object;
  SysprofLocalProfilerPrivate *priv = sysprof_local_profiler_get_instance_private (self);

  if (priv->is_running || priv->is_starting)
    {
      sysprof_local_profiler_stop (SYSPROF_PROFILER (self));
      return;
    }

  sysprof_local_profiler_clear_timer (self);

  G_OBJECT_CLASS (sysprof_local_profiler_parent_class)->dispose (object);
}

static void
sysprof_local_profiler_finalize (GObject *object)
{
  SysprofLocalProfiler *self = (SysprofLocalProfiler *)object;
  SysprofLocalProfilerPrivate *priv = sysprof_local_profiler_get_instance_private (self);

  g_clear_pointer (&priv->writer, sysprof_capture_writer_unref);
  g_clear_pointer (&priv->sources, g_ptr_array_unref);
  g_clear_pointer (&priv->starting, g_ptr_array_unref);
  g_clear_pointer (&priv->stopping, g_ptr_array_unref);
  g_clear_pointer (&priv->failures, g_ptr_array_unref);
  g_clear_pointer (&priv->finished_or_failed, g_ptr_array_unref);
  g_clear_pointer (&priv->pids, g_array_unref);

  G_OBJECT_CLASS (sysprof_local_profiler_parent_class)->finalize (object);
}

static void
sysprof_local_profiler_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  SysprofLocalProfiler *self = SYSPROF_LOCAL_PROFILER (object);
  SysprofLocalProfilerPrivate *priv = sysprof_local_profiler_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_ELAPSED:
      g_value_set_double (value, priv->timer ? g_timer_elapsed (priv->timer, NULL) : 0.0);
      break;

    case PROP_IS_MUTABLE:
      g_value_set_boolean (value, !(priv->is_starting || priv->is_starting || priv->is_running));
      break;

    case PROP_IS_RUNNING:
      g_value_set_boolean (value, priv->is_running);
      break;

    case PROP_WHOLE_SYSTEM:
      g_value_set_boolean (value, priv->whole_system);
      break;

    case PROP_SPAWN:
      g_value_set_boolean (value, priv->spawn);
      break;

    case PROP_SPAWN_INHERIT_ENVIRON:
      g_value_set_boolean (value, priv->spawn_inherit_environ);
      break;

    case PROP_SPAWN_ARGV:
      g_value_set_boxed (value, priv->spawn_argv);
      break;

    case PROP_SPAWN_CWD:
      g_value_set_string (value, priv->spawn_cwd);
      break;

    case PROP_SPAWN_ENV:
      g_value_set_boxed (value, priv->spawn_env);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_local_profiler_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  SysprofLocalProfiler *self = SYSPROF_LOCAL_PROFILER (object);
  SysprofLocalProfilerPrivate *priv = sysprof_local_profiler_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_WHOLE_SYSTEM:
      priv->whole_system = g_value_get_boolean (value);
      break;

    case PROP_SPAWN:
      priv->spawn = g_value_get_boolean (value);
      break;

    case PROP_SPAWN_INHERIT_ENVIRON:
      priv->spawn_inherit_environ = g_value_get_boolean (value);
      break;

    case PROP_SPAWN_ARGV:
      g_strfreev (priv->spawn_argv);
      priv->spawn_argv = g_value_dup_boxed (value);
      break;

    case PROP_SPAWN_CWD:
      g_free (priv->spawn_cwd);
      priv->spawn_cwd = g_value_dup_string (value);
      break;

    case PROP_SPAWN_ENV:
      g_strfreev (priv->spawn_env);
      priv->spawn_env = g_value_dup_boxed (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_local_profiler_class_init (SysprofLocalProfilerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = sysprof_local_profiler_dispose;
  object_class->finalize = sysprof_local_profiler_finalize;
  object_class->get_property = sysprof_local_profiler_get_property;
  object_class->set_property = sysprof_local_profiler_set_property;

  g_object_class_override_property (object_class, PROP_ELAPSED, "elapsed");
  g_object_class_override_property (object_class, PROP_IS_MUTABLE, "is-mutable");
  g_object_class_override_property (object_class, PROP_IS_RUNNING, "is-running");
  g_object_class_override_property (object_class, PROP_SPAWN, "spawn");
  g_object_class_override_property (object_class, PROP_SPAWN_ARGV, "spawn-argv");
  g_object_class_override_property (object_class, PROP_SPAWN_CWD, "spawn-cwd");
  g_object_class_override_property (object_class, PROP_SPAWN_ENV, "spawn-env");
  g_object_class_override_property (object_class, PROP_SPAWN_INHERIT_ENVIRON, "spawn-inherit-environ");
  g_object_class_override_property (object_class, PROP_WHOLE_SYSTEM, "whole-system");

  g_type_ensure (SYSPROF_TYPE_GJS_SOURCE);
#ifdef __linux__
  g_type_ensure (SYSPROF_TYPE_HOSTINFO_SOURCE);
  g_type_ensure (SYSPROF_TYPE_PROC_SOURCE);
  g_type_ensure (SYSPROF_TYPE_PERF_SOURCE);
#endif
  g_type_ensure (SYSPROF_TYPE_PROXY_SOURCE);
}

static void
sysprof_local_profiler_init (SysprofLocalProfiler *self)
{
  SysprofLocalProfilerPrivate *priv = sysprof_local_profiler_get_instance_private (self);

  priv->whole_system = TRUE;

  priv->failures = g_ptr_array_new_with_free_func ((GDestroyNotify)g_error_free);
  priv->sources = g_ptr_array_new_with_free_func (g_object_unref);
  priv->starting = g_ptr_array_new_with_free_func (g_object_unref);
  priv->stopping = g_ptr_array_new_with_free_func (g_object_unref);
  priv->finished_or_failed = g_ptr_array_new_with_free_func (g_object_unref);
  priv->pids = g_array_new (FALSE, FALSE, sizeof (GPid));
}

SysprofProfiler *
sysprof_local_profiler_new (void)
{
  return g_object_new (SYSPROF_TYPE_LOCAL_PROFILER, NULL);
}

static void
sysprof_local_profiler_finish_startup (SysprofLocalProfiler *self)
{
  SysprofLocalProfilerPrivate *priv = sysprof_local_profiler_get_instance_private (self);
  guint i;

  g_assert (SYSPROF_IS_LOCAL_PROFILER (self));
  g_assert (priv->is_starting == TRUE);
  g_assert (priv->starting->len == 0);

  sysprof_local_profiler_clear_timer (self);

  priv->timer = g_timer_new ();

  /*
   * Add a source to update our watchers of elapsed time.
   * We use 1000 instead of add_seconds(1) so that we are
   * not subject to as much drift.
   */
  priv->timer_notify_source =
    g_timeout_add (1000,
                   sysprof_local_profiler_notify_elapsed_cb,
                   self);

  for (i = 0; i < priv->sources->len; i++)
    {
      SysprofSource *source = g_ptr_array_index (priv->sources, i);

      sysprof_source_start (source);
    }

  priv->is_starting = FALSE;

  /*
   * If any of the sources failed during startup, we will have a non-empty
   * failures list.
   */
  if (priv->failures->len > 0)
    {
      const GError *error = g_ptr_array_index (priv->failures, 0);

      g_object_ref (self);
      sysprof_profiler_emit_failed (SYSPROF_PROFILER (self), error);
      sysprof_local_profiler_stop (SYSPROF_PROFILER (self));
      g_object_unref (self);
      return;
    }

  priv->is_running = TRUE;

  g_object_notify (G_OBJECT (self), "is-mutable");
  g_object_notify (G_OBJECT (self), "is-running");

  /*
   * If all the sources are transient (in that they just generate information
   * and then exit), we could be finished as soon as we complete startup.
   *
   * If we detect this, we stop immediately.
   */
  if (priv->finished_or_failed->len == priv->sources->len || priv->stop_after_starting)
    sysprof_local_profiler_stop (SYSPROF_PROFILER (self));
}

static void
sysprof_local_profiler_wait_cb (GObject      *object,
                                GAsyncResult *result,
                                gpointer      user_data)
{
  GSubprocess *subprocess = (GSubprocess *)object;
  g_autoptr(SysprofLocalProfiler) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (G_IS_SUBPROCESS (subprocess));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (SYSPROF_IS_LOCAL_PROFILER (self));

  if (!g_subprocess_wait_finish (subprocess, result, &error))
    g_warning ("Wait on subprocess failed: %s", error->message);

  sysprof_local_profiler_stop (SYSPROF_PROFILER (self));
}

static void
sysprof_local_profiler_start_after_auth (SysprofLocalProfiler *self)
{
  SysprofLocalProfilerPrivate *priv = sysprof_local_profiler_get_instance_private (self);
  g_autofree gchar *keydata = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GKeyFile) keyfile = NULL;
  gsize keylen = 0;

  g_assert (SYSPROF_IS_LOCAL_PROFILER (self));

  keyfile = g_key_file_new ();

  g_key_file_set_boolean (keyfile, "profiler", "whole-system", priv->whole_system);
  if (priv->pids->len > 0)
    g_key_file_set_integer_list (keyfile, "profiler", "pids",
                                 (gint *)(gpointer)priv->pids->data,
                                 priv->pids->len);
  g_key_file_set_boolean (keyfile, "profiler", "spawn", priv->spawn);
  g_key_file_set_boolean (keyfile, "profiler", "spawn-inherit-environ", priv->spawn_inherit_environ);
  g_key_file_set_string (keyfile, "profiler", "spawn-cwd", priv->spawn_cwd ? priv->spawn_cwd : "");

  if (priv->spawn && priv->spawn_argv && priv->spawn_argv[0])
    {
      g_autoptr(GPtrArray) env = g_ptr_array_new_with_free_func (g_free);
      g_autoptr(SysprofSpawnable) spawnable = sysprof_spawnable_new ();
      g_autoptr(GSubprocess) subprocess = NULL;
      GPid pid;

      if (priv->spawn_inherit_environ)
        {
          gchar **environ_ = g_get_environ ();

          for (guint i = 0; environ_[i]; i++)
            g_ptr_array_add (env, environ_[i]);
          g_free (environ_);
        }

      if (priv->spawn_env)
        {
          g_key_file_set_string_list (keyfile, "profiler", "spawn-env",
                                      (const gchar * const *)priv->spawn_env,
                                      g_strv_length (priv->spawn_env));
          for (guint i = 0; priv->spawn_env[i]; i++)
            g_ptr_array_add (env, g_strdup (priv->spawn_env[i]));
        }

      g_ptr_array_add (env, NULL);

      sysprof_spawnable_set_environ (spawnable, (const gchar * const *)env->pdata);
      sysprof_spawnable_append_args (spawnable, (const gchar * const *)priv->spawn_argv);

      if (priv->spawn_cwd != NULL)
        sysprof_spawnable_set_cwd (spawnable, priv->spawn_cwd);

      /* Save argv before modifying */
      if (priv->spawn_argv != NULL)
        g_key_file_set_string_list (keyfile,
                                    "profiler",
                                    "spawn-argv",
                                    (const gchar * const *)priv->spawn_argv,
                                    g_strv_length (priv->spawn_argv));

      for (guint i = 0; i < priv->sources->len; i++)
        {
          SysprofSource *source = g_ptr_array_index (priv->sources, i);

          sysprof_source_modify_spawn (source, spawnable);
        }

      if (!(subprocess = sysprof_spawnable_spawn (spawnable, &error)))
        {
          g_ptr_array_add (priv->failures, g_steal_pointer (&error));
        }
      else
        {
          const gchar *ident = g_subprocess_get_identifier (subprocess);
          pid = atoi (ident);
          g_array_append_val (priv->pids, pid);
          g_subprocess_wait_async (subprocess,
                                   NULL,
                                   sysprof_local_profiler_wait_cb,
                                   g_object_ref (self));
        }
    }

  g_key_file_set_integer (keyfile, "profiler", "n-sources", priv->sources->len);

  for (guint i = 0; i < priv->sources->len; i++)
    {
      SysprofSource *source = g_ptr_array_index (priv->sources, i);
      g_autofree gchar *group = g_strdup_printf ("source-%u", i);

      g_key_file_set_string (keyfile, group, "gtype", G_OBJECT_TYPE_NAME (source));
      sysprof_source_serialize (source, keyfile, group);

      if (priv->whole_system == FALSE)
        {
          for (guint j = 0; j < priv->pids->len; j++)
            {
              GPid pid = g_array_index (priv->pids, GPid, j);

              sysprof_source_add_pid (source, pid);
            }
        }

      sysprof_source_set_writer (source, priv->writer);
      sysprof_source_prepare (source);
    }

  for (guint i = 0; i < priv->sources->len; i++)
    {
      SysprofSource *source = g_ptr_array_index (priv->sources, i);

      if (!sysprof_source_get_is_ready (source))
        g_ptr_array_add (priv->starting, g_object_ref (source));
    }

  if ((keydata = g_key_file_to_data (keyfile, &keylen, NULL)))
    sysprof_capture_writer_add_metadata (priv->writer,
                                         SYSPROF_CAPTURE_CURRENT_TIME,
                                         -1,
                                         -1,
                                         "local-profiler",
                                         keydata,
                                         keylen);

  if (priv->starting->len == 0)
    sysprof_local_profiler_finish_startup (self);
}

static void
sysprof_local_profiler_preroll_cb (GObject      *object,
                                   GAsyncResult *result,
                                   gpointer      user_data)
{
  SysprofHelpers *helpers = (SysprofHelpers *)object;
  g_autoptr(SysprofLocalProfiler) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (SYSPROF_IS_HELPERS (helpers));
  g_assert (SYSPROF_IS_LOCAL_PROFILER (self));

  /* For almost everything at this point, we need to have authorization
   * to the helper daemon. So if this fails, just assume we are going to
   * fail in general. It doesn't really help us to optimize for the case
   * of user-space only profiling since we are rarely used for that.
   */

  if (!sysprof_helpers_authorize_finish (helpers, result, &error))
    sysprof_profiler_emit_failed (SYSPROF_PROFILER (self), error);
  else
    sysprof_local_profiler_start_after_auth (self);
}

static void
sysprof_local_profiler_start (SysprofProfiler *profiler)
{
  SysprofLocalProfiler *self = (SysprofLocalProfiler *)profiler;
  SysprofLocalProfilerPrivate *priv = sysprof_local_profiler_get_instance_private (self);
  g_autoptr(SysprofControlSource) control_source = NULL;

  g_return_if_fail (SYSPROF_IS_LOCAL_PROFILER (self));
  g_return_if_fail (priv->is_running == FALSE);
  g_return_if_fail (priv->is_stopping == FALSE);
  g_return_if_fail (priv->is_starting == FALSE);

  g_clear_pointer (&priv->timer, g_timer_destroy);
  g_object_notify (G_OBJECT (self), "elapsed");

  control_source = sysprof_control_source_new ();
  sysprof_profiler_add_source (SYSPROF_PROFILER (self), SYSPROF_SOURCE (control_source));

  if (priv->writer == NULL)
    {
      SysprofCaptureWriter *writer;
      int fd;

      if ((-1 == (fd = sysprof_memfd_create ("[sysprof]"))) ||
          (NULL == (writer = sysprof_capture_writer_new_from_fd (fd, 0))))
        {
          const GError werror = {
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            (gchar *)g_strerror (errno)
          };

          if (fd != -1)
            close (fd);

          sysprof_profiler_emit_failed (SYSPROF_PROFILER (self), &werror);

          return;
        }

      sysprof_profiler_set_writer (SYSPROF_PROFILER (self), writer);
      g_clear_pointer (&writer, sysprof_capture_writer_unref);
    }

  priv->is_running = TRUE;
  priv->is_starting = TRUE;

  if (priv->failures->len > 0)
    g_ptr_array_remove_range (priv->failures, 0, priv->failures->len);

  /* Start by prefolling our authorization so that future calls are cheap */
  sysprof_helpers_authorize_async (sysprof_helpers_get_default (),
                                   NULL,
                                   sysprof_local_profiler_preroll_cb,
                                   g_object_ref (self));
}

static void
sysprof_local_profiler_set_writer (SysprofProfiler      *profiler,
                                   SysprofCaptureWriter *writer)
{
  SysprofLocalProfiler *self = (SysprofLocalProfiler *)profiler;
  SysprofLocalProfilerPrivate *priv = sysprof_local_profiler_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_LOCAL_PROFILER (self));
  g_return_if_fail (priv->is_running == FALSE);
  g_return_if_fail (priv->is_stopping == FALSE);
  g_return_if_fail (writer != NULL);

  if (priv->writer != writer)
    {
      g_clear_pointer (&priv->writer, sysprof_capture_writer_unref);

      if (writer != NULL)
        priv->writer = sysprof_capture_writer_ref (writer);
    }
}

static void
sysprof_local_profiler_track_completed (SysprofLocalProfiler *self,
                                        SysprofSource        *source)
{
  SysprofLocalProfilerPrivate *priv = sysprof_local_profiler_get_instance_private (self);
  gint i;

  g_assert (SYSPROF_IS_LOCAL_PROFILER (self));
  g_assert (SYSPROF_IS_SOURCE (source));

  if (!_g_ptr_array_contains (priv->finished_or_failed, source))
    g_ptr_array_add (priv->finished_or_failed, g_object_ref (source));

  if (priv->is_starting)
    {
      i = _g_ptr_array_find (priv->starting, source);

      if (i >= 0)
        {
          g_ptr_array_remove_index (priv->starting, i);
          if (priv->starting->len == 0)
            sysprof_local_profiler_finish_startup (self);
        }
    }

  if (priv->is_stopping)
    {
      i = _g_ptr_array_find (priv->stopping, source);

      if (i >= 0)
        {
          g_ptr_array_remove_index_fast (priv->stopping, i);

          if ((priv->is_stopping == TRUE) && (priv->stopping->len == 0))
            sysprof_local_profiler_finish_stopping (self);
        }
    }

  if (!priv->is_starting)
    {
      if (priv->finished_or_failed->len == priv->sources->len)
        sysprof_local_profiler_stop (SYSPROF_PROFILER (self));
    }
}

static void
sysprof_local_profiler_source_finished (SysprofLocalProfiler *self,
                                        SysprofSource        *source)
{
  g_assert (SYSPROF_IS_LOCAL_PROFILER (self));
  g_assert (SYSPROF_IS_SOURCE (source));

  sysprof_local_profiler_track_completed (self, source);
}

static void
sysprof_local_profiler_source_ready (SysprofLocalProfiler *self,
                                     SysprofSource        *source)
{
  SysprofLocalProfilerPrivate *priv = sysprof_local_profiler_get_instance_private (self);
  guint i;

  g_assert (SYSPROF_IS_LOCAL_PROFILER (self));
  g_assert (SYSPROF_IS_SOURCE (source));

  for (i = 0; i < priv->starting->len; i++)
    {
      SysprofSource *ele = g_ptr_array_index (priv->starting, i);

      if (ele == source)
        {
          g_ptr_array_remove_index_fast (priv->starting, i);

          if ((priv->is_starting == TRUE) && (priv->starting->len == 0))
            sysprof_local_profiler_finish_startup (self);

          break;
        }
    }
}

static void
sysprof_local_profiler_source_failed (SysprofLocalProfiler *self,
                                      const GError         *reason,
                                      SysprofSource        *source)
{
  SysprofLocalProfilerPrivate *priv = sysprof_local_profiler_get_instance_private (self);

  g_assert (SYSPROF_IS_LOCAL_PROFILER (self));
  g_assert (reason != NULL);
  g_assert (SYSPROF_IS_SOURCE (source));

  g_warning ("%s failed: %s",
             G_OBJECT_TYPE_NAME (source),
             reason ? reason->message : "unknown error");

  sysprof_local_profiler_track_completed (self, source);

  /* Failure emitted out of band */
  if (!priv->is_starting && !priv->is_stopping && !priv->is_running)
    return;

  g_ptr_array_add (priv->failures, g_error_copy (reason));

  /* Ignore during start/stop, we handle this in other places */
  if (priv->is_starting || priv->is_stopping)
    return;

  if (priv->is_running)
    sysprof_local_profiler_stop (SYSPROF_PROFILER (self));
}

static void
sysprof_local_profiler_add_source (SysprofProfiler *profiler,
                                   SysprofSource   *source)
{
  SysprofLocalProfiler *self = (SysprofLocalProfiler *)profiler;
  SysprofLocalProfilerPrivate *priv = sysprof_local_profiler_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_LOCAL_PROFILER (self));
  g_return_if_fail (SYSPROF_IS_SOURCE (source));
  g_return_if_fail (priv->is_running == FALSE);
  g_return_if_fail (priv->is_starting == FALSE);
  g_return_if_fail (priv->is_stopping == FALSE);

  g_signal_connect_object (source,
                           "failed",
                           G_CALLBACK (sysprof_local_profiler_source_failed),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (source,
                           "finished",
                           G_CALLBACK (sysprof_local_profiler_source_finished),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (source,
                           "ready",
                           G_CALLBACK (sysprof_local_profiler_source_ready),
                           self,
                           G_CONNECT_SWAPPED);


  g_ptr_array_add (priv->sources, g_object_ref (source));
}

static void
sysprof_local_profiler_add_pid (SysprofProfiler *profiler,
                                GPid             pid)
{
  SysprofLocalProfiler *self = (SysprofLocalProfiler *)profiler;
  SysprofLocalProfilerPrivate *priv = sysprof_local_profiler_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_LOCAL_PROFILER (self));
  g_return_if_fail (pid > -1);
  g_return_if_fail (priv->is_starting == FALSE);
  g_return_if_fail (priv->is_stopping == FALSE);
  g_return_if_fail (priv->is_running == FALSE);

  g_array_append_val (priv->pids, pid);
}

static void
sysprof_local_profiler_remove_pid (SysprofProfiler *profiler,
                                   GPid             pid)
{
  SysprofLocalProfiler *self = (SysprofLocalProfiler *)profiler;
  SysprofLocalProfilerPrivate *priv = sysprof_local_profiler_get_instance_private (self);
  guint i;

  g_return_if_fail (SYSPROF_IS_LOCAL_PROFILER (self));
  g_return_if_fail (pid > -1);
  g_return_if_fail (priv->is_starting == FALSE);
  g_return_if_fail (priv->is_stopping == FALSE);
  g_return_if_fail (priv->is_running == FALSE);

  for (i = 0; i < priv->pids->len; i++)
    {
      GPid ele = g_array_index (priv->pids, GPid, i);

      if (ele == pid)
        {
          g_array_remove_index_fast (priv->pids, i);
          break;
        }
    }
}

static const GPid *
sysprof_local_profiler_get_pids (SysprofProfiler *profiler,
                                 guint           *n_pids)
{
  SysprofLocalProfiler *self = (SysprofLocalProfiler *)profiler;
  SysprofLocalProfilerPrivate *priv = sysprof_local_profiler_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_LOCAL_PROFILER (self), NULL);
  g_return_val_if_fail (n_pids != NULL, NULL);

  *n_pids = priv->pids->len;

  return (GPid *)(gpointer)priv->pids->data;
}

static SysprofCaptureWriter *
sysprof_local_profiler_get_writer (SysprofProfiler *profiler)
{
  SysprofLocalProfiler *self = (SysprofLocalProfiler *)profiler;
  SysprofLocalProfilerPrivate *priv = sysprof_local_profiler_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_LOCAL_PROFILER (self), NULL);

  return priv->writer;
}

static void
profiler_iface_init (SysprofProfilerInterface *iface)
{
  iface->add_pid = sysprof_local_profiler_add_pid;
  iface->add_source = sysprof_local_profiler_add_source;
  iface->get_pids = sysprof_local_profiler_get_pids;
  iface->get_writer = sysprof_local_profiler_get_writer;
  iface->remove_pid = sysprof_local_profiler_remove_pid;
  iface->set_writer = sysprof_local_profiler_set_writer;
  iface->start = sysprof_local_profiler_start;
  iface->stop = sysprof_local_profiler_stop;
  iface->stopped = sysprof_local_profiler_real_stopped;
}

static bool
find_profiler_meta_cb (const SysprofCaptureFrame *frame,
                       void                      *user_data)
{
  const SysprofCaptureMetadata *meta = (const SysprofCaptureMetadata *)frame;
  GKeyFile **keyfile = user_data;

  g_assert (frame != NULL);
  g_assert (frame->type == SYSPROF_CAPTURE_FRAME_METADATA);
  g_assert (keyfile != NULL);
  g_assert (*keyfile == NULL);

  if (g_strcmp0 (meta->id, "local-profiler") == 0)
    {
      g_autoptr(GKeyFile) kf = g_key_file_new ();

      /* Metadata is guaranteed to be \0 terminated by marshaller */
      if (g_key_file_load_from_data (kf, meta->metadata, -1, 0, NULL))
        *keyfile = g_steal_pointer (&kf);

      return *keyfile == NULL;
    }

  return true;
}

SysprofProfiler *
sysprof_local_profiler_new_replay (SysprofCaptureReader *reader)
{
  static const SysprofCaptureFrameType mtype[] = {
    SYSPROF_CAPTURE_FRAME_METADATA,
  };
  g_autoptr(SysprofLocalProfiler) self = NULL;
  g_autoptr(SysprofCaptureCursor) cursor = NULL;
  g_autoptr(GKeyFile) keyfile = NULL;
  g_autofree gchar *cwd = NULL;
  g_auto(GStrv) argv = NULL;
  g_auto(GStrv) env = NULL;
  gboolean whole_system;
  gboolean inherit;
  gboolean spawn;
  gint n_sources;

  g_return_val_if_fail (reader != NULL, NULL);

  self = g_object_new (SYSPROF_TYPE_LOCAL_PROFILER, NULL);

  cursor = sysprof_capture_cursor_new (reader);
  sysprof_capture_cursor_add_condition (cursor, sysprof_capture_condition_new_where_type_in (1, mtype));
  sysprof_capture_cursor_foreach (cursor, find_profiler_meta_cb, &keyfile);

  /* No metadata, bail */
  if (keyfile == NULL)
    return NULL;

  spawn = g_key_file_get_boolean (keyfile, "profiler", "spawn", NULL);
  inherit = g_key_file_get_boolean (keyfile, "profiler", "spawn-inherit-environ", NULL);
  argv = g_key_file_get_string_list (keyfile, "profiler", "spawn-argv", NULL, NULL);
  env = g_key_file_get_string_list (keyfile, "profiler", "spawn-env", NULL, NULL);
  cwd = g_key_file_get_string (keyfile, "profiler", "spawn-cwd", NULL);
  n_sources = g_key_file_get_integer (keyfile, "profiler", "n-sources", NULL);
  whole_system = g_key_file_get_boolean (keyfile, "profiler", "whole-system", NULL);

  /* Ignore empty string */
  if (cwd != NULL && *cwd == 0)
    g_clear_pointer (&cwd, g_free);

  sysprof_profiler_set_spawn (SYSPROF_PROFILER (self), spawn);
  sysprof_profiler_set_spawn_argv (SYSPROF_PROFILER (self), CSTRV (argv));
  sysprof_profiler_set_spawn_cwd (SYSPROF_PROFILER (self), cwd);
  sysprof_profiler_set_spawn_env (SYSPROF_PROFILER (self), CSTRV (env));
  sysprof_profiler_set_spawn_inherit_environ (SYSPROF_PROFILER (self), inherit);
  sysprof_profiler_set_whole_system (SYSPROF_PROFILER (self), whole_system);

  for (guint i = 0; i < n_sources; i++)
    {
      g_autofree gchar *group = g_strdup_printf ("source-%u", i);
      g_autofree gchar *type_name = NULL;
      g_autoptr(SysprofSource) source = NULL;
      GType gtype;

      if (!g_key_file_has_group (keyfile, group) ||
          !(type_name = g_key_file_get_string (keyfile, group, "gtype", NULL)) ||
          !(gtype = g_type_from_name (type_name)) ||
          !g_type_is_a (gtype, SYSPROF_TYPE_SOURCE) ||
          !(source = g_object_new (gtype, NULL)))
        continue;

      sysprof_source_deserialize (source, keyfile, group);
      sysprof_local_profiler_add_source (SYSPROF_PROFILER (self), source);
    }

  return SYSPROF_PROFILER (g_steal_pointer (&self));
}
