/* sp-local-profiler.c
 *
 * Copyright (C) 2016 Christian Hergert <chergert@redhat.com>
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
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <unistd.h>

#include "profiler/sp-local-profiler.h"
#include "util/sp-platform.h"

typedef struct
{
  SpCaptureWriter *writer;

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
} SpLocalProfilerPrivate;

static void profiler_iface_init (SpProfilerInterface *iface);

G_DEFINE_TYPE_EXTENDED (SpLocalProfiler, sp_local_profiler, G_TYPE_OBJECT, 0,
                        G_ADD_PRIVATE (SpLocalProfiler)
                        G_IMPLEMENT_INTERFACE (SP_TYPE_PROFILER, profiler_iface_init))

enum {
  PROP_0,
  N_PROPS,

  PROP_ELAPSED,
  PROP_IS_MUTABLE,
  PROP_IS_RUNNING,
  PROP_SPAWN,
  PROP_SPAWN_ARGV,
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
sp_local_profiler_clear_timer (SpLocalProfiler *self)
{
  SpLocalProfilerPrivate *priv = sp_local_profiler_get_instance_private (self);

  g_assert (SP_IS_LOCAL_PROFILER (self));

  g_clear_pointer (&priv->timer, g_timer_destroy);

  if (priv->timer_notify_source != 0)
    {
      g_source_remove (priv->timer_notify_source);
      priv->timer_notify_source = 0;
    }
}

static void
sp_local_profiler_real_stopped (SpProfiler *profiler)
{
  SpLocalProfiler *self = (SpLocalProfiler *)profiler;

  g_assert (SP_IS_LOCAL_PROFILER (self));

  sp_local_profiler_clear_timer (self);
}

static gboolean
sp_local_profiler_notify_elapsed_cb (gpointer data)
{
  SpLocalProfiler *self = data;

  g_assert (SP_IS_LOCAL_PROFILER (self));

  g_object_notify (G_OBJECT (self), "elapsed");

  return G_SOURCE_CONTINUE;
}

static void
sp_local_profiler_finish_stopping (SpLocalProfiler *self)
{
  SpLocalProfilerPrivate *priv = sp_local_profiler_get_instance_private (self);

  g_assert (SP_IS_LOCAL_PROFILER (self));
  g_assert (priv->is_starting == FALSE);
  g_assert (priv->is_stopping == TRUE);
  g_assert (priv->stopping->len == 0);

  if (priv->failures->len > 0)
    {
      const GError *error = g_ptr_array_index (priv->failures, 0);

      sp_profiler_emit_failed (SP_PROFILER (self), error);
    }

  priv->is_running = FALSE;
  priv->is_stopping = FALSE;

  sp_profiler_emit_stopped (SP_PROFILER (self));

  g_object_notify (G_OBJECT (self), "is-mutable");
  g_object_notify (G_OBJECT (self), "is-running");
}

static void
sp_local_profiler_stop (SpProfiler *profiler)
{
  SpLocalProfiler *self = (SpLocalProfiler *)profiler;
  SpLocalProfilerPrivate *priv = sp_local_profiler_get_instance_private (self);
  guint i;

  g_return_if_fail (SP_IS_LOCAL_PROFILER (self));

  if (priv->is_stopping || (!priv->is_starting && !priv->is_running))
    return;

  priv->is_stopping = TRUE;

  /*
   * First we add everything to the stopping list, so that we can
   * be notified of when they have completed. If everything stopped
   * synchronously, the stopping list will be empty after calling
   * sp_source_stop() for every source. Otherwise, we need to delay
   * stopping for a little bit.
   */

  for (i = 0; i < priv->sources->len; i++)
    {
      SpSource *source = g_ptr_array_index (priv->sources, i);

      if (!_g_ptr_array_contains (priv->finished_or_failed, source))
        g_ptr_array_add (priv->stopping, g_object_ref (source));
    }

  for (i = 0; i < priv->sources->len; i++)
    {
      SpSource *source = g_ptr_array_index (priv->sources, i);

      sp_source_stop (source);
    }

  if (priv->is_stopping && priv->stopping->len == 0)
    sp_local_profiler_finish_stopping (self);
}


static void
sp_local_profiler_dispose (GObject *object)
{
  SpLocalProfiler *self = (SpLocalProfiler *)object;
  SpLocalProfilerPrivate *priv = sp_local_profiler_get_instance_private (self);

  if (priv->is_running || priv->is_starting)
    {
      sp_local_profiler_stop (SP_PROFILER (self));
      return;
    }

  sp_local_profiler_clear_timer (self);

  G_OBJECT_CLASS (sp_local_profiler_parent_class)->dispose (object);
}

static void
sp_local_profiler_finalize (GObject *object)
{
  SpLocalProfiler *self = (SpLocalProfiler *)object;
  SpLocalProfilerPrivate *priv = sp_local_profiler_get_instance_private (self);

  g_clear_pointer (&priv->writer, sp_capture_writer_unref);
  g_clear_pointer (&priv->sources, g_ptr_array_unref);
  g_clear_pointer (&priv->starting, g_ptr_array_unref);
  g_clear_pointer (&priv->stopping, g_ptr_array_unref);
  g_clear_pointer (&priv->failures, g_ptr_array_unref);
  g_clear_pointer (&priv->finished_or_failed, g_ptr_array_unref);
  g_clear_pointer (&priv->pids, g_array_unref);

  G_OBJECT_CLASS (sp_local_profiler_parent_class)->finalize (object);
}

static void
sp_local_profiler_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  SpLocalProfiler *self = SP_LOCAL_PROFILER (object);
  SpLocalProfilerPrivate *priv = sp_local_profiler_get_instance_private (self);

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

    case PROP_SPAWN_ENV:
      g_value_set_boxed (value, priv->spawn_env);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_local_profiler_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  SpLocalProfiler *self = SP_LOCAL_PROFILER (object);
  SpLocalProfilerPrivate *priv = sp_local_profiler_get_instance_private (self);

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

    case PROP_SPAWN_ENV:
      g_strfreev (priv->spawn_env);
      priv->spawn_env = g_value_dup_boxed (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_local_profiler_class_init (SpLocalProfilerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = sp_local_profiler_dispose;
  object_class->finalize = sp_local_profiler_finalize;
  object_class->get_property = sp_local_profiler_get_property;
  object_class->set_property = sp_local_profiler_set_property;

  g_object_class_override_property (object_class, PROP_ELAPSED, "elapsed");
  g_object_class_override_property (object_class, PROP_IS_MUTABLE, "is-mutable");
  g_object_class_override_property (object_class, PROP_IS_RUNNING, "is-running");
  g_object_class_override_property (object_class, PROP_SPAWN, "spawn");
  g_object_class_override_property (object_class, PROP_SPAWN_ARGV, "spawn-argv");
  g_object_class_override_property (object_class, PROP_SPAWN_ENV, "spawn-env");
  g_object_class_override_property (object_class, PROP_SPAWN_INHERIT_ENVIRON, "spawn-inherit-environ");
  g_object_class_override_property (object_class, PROP_WHOLE_SYSTEM, "whole-system");
}

static void
sp_local_profiler_init (SpLocalProfiler *self)
{
  SpLocalProfilerPrivate *priv = sp_local_profiler_get_instance_private (self);

  priv->whole_system = TRUE;

  priv->failures = g_ptr_array_new_with_free_func ((GDestroyNotify)g_error_free);
  priv->sources = g_ptr_array_new_with_free_func (g_object_unref);
  priv->starting = g_ptr_array_new_with_free_func (g_object_unref);
  priv->stopping = g_ptr_array_new_with_free_func (g_object_unref);
  priv->finished_or_failed = g_ptr_array_new_with_free_func (g_object_unref);
  priv->pids = g_array_new (FALSE, FALSE, sizeof (GPid));
}

SpProfiler *
sp_local_profiler_new (void)
{
  return g_object_new (SP_TYPE_LOCAL_PROFILER, NULL);
}

static void
sp_local_profiler_finish_startup (SpLocalProfiler *self)
{
  SpLocalProfilerPrivate *priv = sp_local_profiler_get_instance_private (self);
  guint i;

  g_assert (SP_IS_LOCAL_PROFILER (self));
  g_assert (priv->is_starting == TRUE);
  g_assert (priv->starting->len == 0);

  sp_local_profiler_clear_timer (self);

  priv->timer = g_timer_new ();

  /*
   * Add a source to update our watchers of elapsed time.
   * We use 1000 instead of add_seconds(1) so that we are
   * not subject to as much drift.
   */
  priv->timer_notify_source =
    g_timeout_add (1000,
                   sp_local_profiler_notify_elapsed_cb,
                   self);

  for (i = 0; i < priv->sources->len; i++)
    {
      SpSource *source = g_ptr_array_index (priv->sources, i);

      sp_source_start (source);
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
      sp_profiler_emit_failed (SP_PROFILER (self), error);
      sp_local_profiler_stop (SP_PROFILER (self));
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
  if (priv->finished_or_failed->len == priv->sources->len)
    sp_local_profiler_stop (SP_PROFILER (self));
}

static void
sp_local_profiler_start (SpProfiler *profiler)
{
  SpLocalProfiler *self = (SpLocalProfiler *)profiler;
  SpLocalProfilerPrivate *priv = sp_local_profiler_get_instance_private (self);
  guint i;

  g_return_if_fail (SP_IS_LOCAL_PROFILER (self));
  g_return_if_fail (priv->is_running == FALSE);
  g_return_if_fail (priv->is_stopping == FALSE);
  g_return_if_fail (priv->is_starting == FALSE);

  g_clear_pointer (&priv->timer, g_timer_destroy);
  g_object_notify (G_OBJECT (self), "elapsed");

  if (priv->writer == NULL)
    {
      SpCaptureWriter *writer;
      int fd;

      if ((-1 == (fd = sp_memfd_create ("[sysprof]"))) ||
          (NULL == (writer = sp_capture_writer_new_from_fd (fd, 0))))
        {
          const GError error = {
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            (gchar *)g_strerror (errno)
          };

          if (fd != -1)
            close (fd);

          sp_profiler_emit_failed (SP_PROFILER (self), &error);

          return;
        }

      sp_profiler_set_writer (SP_PROFILER (self), writer);
      g_clear_pointer (&writer, sp_capture_writer_unref);
    }

  priv->is_running = TRUE;
  priv->is_starting = TRUE;

  if (priv->failures->len > 0)
    g_ptr_array_remove_range (priv->failures, 0, priv->failures->len);

  if (priv->spawn && priv->spawn_argv && priv->spawn_argv[0])
    {
      g_autoptr(GPtrArray) ar = g_ptr_array_new_with_free_func (g_free);
      GPid pid;
      GError *error = NULL;

      if (priv->spawn_inherit_environ)
        {
          gchar **environ = g_get_environ ();

          for (i = 0; environ[i]; i++)
            g_ptr_array_add (ar, environ[i]);
          g_free (environ);
        }

      if (priv->spawn_env)
        {
          for (i = 0; priv->spawn_env[i]; i++)
            g_ptr_array_add (ar, g_strdup (priv->spawn_env[i]));
        }

      g_ptr_array_add (ar, NULL);

      if (!g_spawn_async (g_get_home_dir (),
                          priv->spawn_argv,
                          (gchar **)ar->pdata,
                          (G_SPAWN_SEARCH_PATH |
                           G_SPAWN_STDOUT_TO_DEV_NULL |
                           G_SPAWN_STDOUT_TO_DEV_NULL),
                          NULL,
                          NULL,
                          &pid,
                          &error))
        g_ptr_array_add (priv->failures, error);
      else
        g_array_append_val (priv->pids, pid);
    }

  for (i = 0; i < priv->sources->len; i++)
    {
      SpSource *source = g_ptr_array_index (priv->sources, i);
      guint j;

      if (priv->whole_system == FALSE)
        {
          for (j = 0; j < priv->pids->len; j++)
            {
              GPid pid = g_array_index (priv->pids, GPid, j);

              sp_source_add_pid (source, pid);
            }
        }

      sp_source_set_writer (source, priv->writer);
      sp_source_prepare (source);
    }

  for (i = 0; i < priv->sources->len; i++)
    {
      SpSource *source = g_ptr_array_index (priv->sources, i);

      if (!sp_source_get_is_ready (source))
        g_ptr_array_add (priv->starting, g_object_ref (source));
    }

  if (priv->starting->len == 0)
    sp_local_profiler_finish_startup (self);
}

static void
sp_local_profiler_set_writer (SpProfiler      *profiler,
                              SpCaptureWriter *writer)
{
  SpLocalProfiler *self = (SpLocalProfiler *)profiler;
  SpLocalProfilerPrivate *priv = sp_local_profiler_get_instance_private (self);

  g_return_if_fail (SP_IS_LOCAL_PROFILER (self));
  g_return_if_fail (priv->is_running == FALSE);
  g_return_if_fail (priv->is_stopping == FALSE);
  g_return_if_fail (writer != NULL);

  if (priv->writer != writer)
    {
      g_clear_pointer (&priv->writer, sp_capture_writer_unref);

      if (writer != NULL)
        priv->writer = sp_capture_writer_ref (writer);
    }
}

static void
sp_local_profiler_track_completed (SpLocalProfiler *self,
                                   SpSource        *source)
{
  SpLocalProfilerPrivate *priv = sp_local_profiler_get_instance_private (self);
  gint i;

  g_assert (SP_IS_LOCAL_PROFILER (self));
  g_assert (SP_IS_SOURCE (source));

  if (!_g_ptr_array_contains (priv->finished_or_failed, source))
    g_ptr_array_add (priv->finished_or_failed, g_object_ref (source));

  if (priv->is_starting)
    {
      i = _g_ptr_array_find (priv->starting, source);

      if (i >= 0)
        {
          g_ptr_array_remove_index (priv->starting, i);
          if (priv->starting->len == 0)
            sp_local_profiler_finish_startup (self);
        }
    }

  if (priv->is_stopping)
    {
      i = _g_ptr_array_find (priv->stopping, source);

      if (i >= 0)
        {
          g_ptr_array_remove_index_fast (priv->stopping, i);

          if ((priv->is_stopping == TRUE) && (priv->stopping->len == 0))
            sp_local_profiler_finish_stopping (self);
        }
    }

  if (!priv->is_starting)
    {
      if (priv->finished_or_failed->len == priv->sources->len)
        sp_local_profiler_stop (SP_PROFILER (self));
    }
}

static void
sp_local_profiler_source_finished (SpLocalProfiler *self,
                                   SpSource        *source)
{
  g_assert (SP_IS_LOCAL_PROFILER (self));
  g_assert (SP_IS_SOURCE (source));

  sp_local_profiler_track_completed (self, source);
}

static void
sp_local_profiler_source_ready (SpLocalProfiler *self,
                                SpSource        *source)
{
  SpLocalProfilerPrivate *priv = sp_local_profiler_get_instance_private (self);
  guint i;

  g_assert (SP_IS_LOCAL_PROFILER (self));
  g_assert (SP_IS_SOURCE (source));

  for (i = 0; i < priv->starting->len; i++)
    {
      SpSource *ele = g_ptr_array_index (priv->starting, i);

      if (ele == source)
        {
          g_ptr_array_remove_index_fast (priv->starting, i);

          if ((priv->is_starting == TRUE) && (priv->starting->len == 0))
            sp_local_profiler_finish_startup (self);

          break;
        }
    }
}

static void
sp_local_profiler_source_failed (SpLocalProfiler *self,
                                 const GError    *reason,
                                 SpSource        *source)
{
  SpLocalProfilerPrivate *priv = sp_local_profiler_get_instance_private (self);

  g_assert (SP_IS_LOCAL_PROFILER (self));
  g_assert (reason != NULL);
  g_assert (SP_IS_SOURCE (source));

  sp_local_profiler_track_completed (self, source);

  /* Failure emitted out of band */
  if (!priv->is_starting && !priv->is_stopping && !priv->is_running)
    return;

  g_ptr_array_add (priv->failures, g_error_copy (reason));

  /* Ignore during start/stop, we handle this in other places */
  if (priv->is_starting || priv->is_stopping)
    return;

  if (priv->is_running)
    sp_local_profiler_stop (SP_PROFILER (self));
}

static void
sp_local_profiler_add_source (SpProfiler *profiler,
                              SpSource   *source)
{
  SpLocalProfiler *self = (SpLocalProfiler *)profiler;
  SpLocalProfilerPrivate *priv = sp_local_profiler_get_instance_private (self);

  g_return_if_fail (SP_IS_LOCAL_PROFILER (self));
  g_return_if_fail (SP_IS_SOURCE (source));
  g_return_if_fail (priv->is_running == FALSE);
  g_return_if_fail (priv->is_starting == FALSE);
  g_return_if_fail (priv->is_stopping == FALSE);

  g_signal_connect_object (source,
                           "failed",
                           G_CALLBACK (sp_local_profiler_source_failed),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (source,
                           "finished",
                           G_CALLBACK (sp_local_profiler_source_finished),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (source,
                           "ready",
                           G_CALLBACK (sp_local_profiler_source_ready),
                           self,
                           G_CONNECT_SWAPPED);


  g_ptr_array_add (priv->sources, g_object_ref (source));
}

static void
sp_local_profiler_add_pid (SpProfiler *profiler,
                           GPid        pid)
{
  SpLocalProfiler *self = (SpLocalProfiler *)profiler;
  SpLocalProfilerPrivate *priv = sp_local_profiler_get_instance_private (self);

  g_return_if_fail (SP_IS_LOCAL_PROFILER (self));
  g_return_if_fail (pid > -1);
  g_return_if_fail (priv->is_starting == FALSE);
  g_return_if_fail (priv->is_stopping == FALSE);
  g_return_if_fail (priv->is_running == FALSE);

  g_array_append_val (priv->pids, pid);
}

static void
sp_local_profiler_remove_pid (SpProfiler *profiler,
                              GPid        pid)
{
  SpLocalProfiler *self = (SpLocalProfiler *)profiler;
  SpLocalProfilerPrivate *priv = sp_local_profiler_get_instance_private (self);
  guint i;

  g_return_if_fail (SP_IS_LOCAL_PROFILER (self));
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
sp_local_profiler_get_pids (SpProfiler *profiler,
                            guint      *n_pids)
{
  SpLocalProfiler *self = (SpLocalProfiler *)profiler;
  SpLocalProfilerPrivate *priv = sp_local_profiler_get_instance_private (self);

  g_return_val_if_fail (SP_IS_LOCAL_PROFILER (self), NULL);
  g_return_val_if_fail (n_pids != NULL, NULL);

  *n_pids = priv->pids->len;

  return (GPid *)(gpointer)priv->pids->data;
}

static SpCaptureWriter *
sp_local_profiler_get_writer (SpProfiler *profiler)
{
  SpLocalProfiler *self = (SpLocalProfiler *)profiler;
  SpLocalProfilerPrivate *priv = sp_local_profiler_get_instance_private (self);

  g_return_val_if_fail (SP_IS_LOCAL_PROFILER (self), NULL);

  return priv->writer;
}

static void
profiler_iface_init (SpProfilerInterface *iface)
{
  iface->add_pid = sp_local_profiler_add_pid;
  iface->add_source = sp_local_profiler_add_source;
  iface->get_pids = sp_local_profiler_get_pids;
  iface->get_writer = sp_local_profiler_get_writer;
  iface->remove_pid = sp_local_profiler_remove_pid;
  iface->set_writer = sp_local_profiler_set_writer;
  iface->start = sp_local_profiler_start;
  iface->stop = sp_local_profiler_stop;
  iface->stopped = sp_local_profiler_real_stopped;
}
