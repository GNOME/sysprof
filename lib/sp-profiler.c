/* sp-profiler.c
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
#include <sys/syscall.h>
#include <unistd.h>

#include "sp-profiler.h"

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
} SpProfilerPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (SpProfiler, sp_profiler, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_IS_MUTABLE,
  PROP_IS_RUNNING,
  PROP_ELAPSED,
  PROP_SPAWN,
  PROP_SPAWN_INHERIT_ENVIRON,
  PROP_WHOLE_SYSTEM,
  N_PROPS
};

enum {
  FAILED,
  STOPPED,
  N_SIGNALS
};

static GParamSpec *properties [N_PROPS];
static guint signals [N_SIGNALS];

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

gdouble
sp_profiler_get_elapsed (SpProfiler *self)
{
  SpProfilerPrivate *priv = sp_profiler_get_instance_private (self);

  g_return_val_if_fail (SP_IS_PROFILER (self), 0.0);

  return (priv->timer != NULL) ? g_timer_elapsed (priv->timer, NULL) : 0.0;
}

static void
sp_profiler_clear_timer (SpProfiler *self)
{
  SpProfilerPrivate *priv = sp_profiler_get_instance_private (self);

  g_assert (SP_IS_PROFILER (self));

  g_clear_pointer (&priv->timer, g_timer_destroy);

  if (priv->timer_notify_source != 0)
    {
      g_source_remove (priv->timer_notify_source);
      priv->timer_notify_source = 0;
    }
}

static void
sp_profiler_real_stopped (SpProfiler *self)
{
  g_assert (SP_IS_PROFILER (self));

  sp_profiler_clear_timer (self);
}

static gboolean
sp_profiler_notify_elapsed_cb (gpointer data)
{
  SpProfiler *self = data;

  g_assert (SP_IS_PROFILER (self));

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_ELAPSED]);

  return G_SOURCE_CONTINUE;
}

static void
sp_profiler_dispose (GObject *object)
{
  SpProfiler *self = (SpProfiler *)object;

  if (sp_profiler_get_is_running (self))
    {
      sp_profiler_stop (self);
      return;
    }

  sp_profiler_clear_timer (self);

  G_OBJECT_CLASS (sp_profiler_parent_class)->dispose (object);
}

static void
sp_profiler_finalize (GObject *object)
{
  SpProfiler *self = (SpProfiler *)object;
  SpProfilerPrivate *priv = sp_profiler_get_instance_private (self);

  g_clear_pointer (&priv->writer, sp_capture_writer_unref);
  g_clear_pointer (&priv->sources, g_ptr_array_unref);
  g_clear_pointer (&priv->starting, g_ptr_array_unref);
  g_clear_pointer (&priv->stopping, g_ptr_array_unref);
  g_clear_pointer (&priv->finished_or_failed, g_ptr_array_unref);
  g_clear_pointer (&priv->pids, g_array_unref);

  G_OBJECT_CLASS (sp_profiler_parent_class)->finalize (object);
}

static void
sp_profiler_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  SpProfiler *self = SP_PROFILER (object);

  switch (prop_id)
    {
    case PROP_IS_MUTABLE:
      g_value_set_boolean (value, sp_profiler_get_is_mutable (self));
      break;

    case PROP_IS_RUNNING:
      g_value_set_boolean (value, sp_profiler_get_is_running (self));
      break;

    case PROP_WHOLE_SYSTEM:
      g_value_set_boolean (value, sp_profiler_get_whole_system (self));
      break;

    case PROP_SPAWN:
      g_value_set_boolean (value, sp_profiler_get_spawn (self));
      break;

    case PROP_SPAWN_INHERIT_ENVIRON:
      g_value_set_boolean (value, sp_profiler_get_spawn_inherit_environ (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_profiler_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  SpProfiler *self = SP_PROFILER (object);

  switch (prop_id)
    {
    case PROP_WHOLE_SYSTEM:
      sp_profiler_set_whole_system (self, g_value_get_boolean (value));
      break;

    case PROP_SPAWN:
      sp_profiler_set_spawn (self, g_value_get_boolean (value));
      break;

    case PROP_SPAWN_INHERIT_ENVIRON:
      sp_profiler_set_spawn_inherit_environ (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_profiler_class_init (SpProfilerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = sp_profiler_dispose;
  object_class->finalize = sp_profiler_finalize;
  object_class->get_property = sp_profiler_get_property;
  object_class->set_property = sp_profiler_set_property;

  klass->stopped = sp_profiler_real_stopped;

  /**
   * SpProfiler:elapsed:
   *
   * This property is updated on a second basis while recording so that
   * UIs can keep a timer of the elapsed time while recording.
   *
   * It contains a double with seconds as whole integers and fractions
   * of second after the decimal point.
   */
  properties [PROP_ELAPSED] =
    g_param_spec_double ("elapsed",
                         "Elapsed Time",
                         "The amount of time elapsed while recording",
                         0.0,
                         G_MAXDOUBLE,
                         0.0,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * SpProfiler:is-running:
   *
   * If the profiler has been started. Note that after being started, this
   * property won't change back to %FALSE until all sources have stopped
   * and notified of asynchronous completion.
   */
  properties [PROP_IS_RUNNING] =
    g_param_spec_boolean ("is-running",
                          "Is Running",
                          "If the profiler has been started",
                          FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * SpProfiler:is-mutable:
   *
   * This property is useful from a UI standpoint to desensitize
   * configuration widgets once the profiler can no longer be modified.
   */
  properties [PROP_IS_MUTABLE] =
    g_param_spec_boolean ("is-mutable",
                          "Is Mutable",
                          "If the profiler can be modified",
                          FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SPAWN] =
    g_param_spec_boolean ("spawn",
                          "Spawn",
                          "If a child should process should be spawned",
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_SPAWN_INHERIT_ENVIRON] =
    g_param_spec_boolean ("spawn-inherit-environ",
                          "Spawn Inherit Environ",
                          "If a child should inherit the current environment",
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  /**
   * SpProfiler:whole-system:
   *
   * This property denotes if the whole system should be profiled instead of
   * a single process. This is useful for UI to toggle between process
   * selection and all processes.
   *
   * Setting this to %TRUE will result in the pids added to be ignored
   * during startup.
   */
  properties [PROP_WHOLE_SYSTEM] =
    g_param_spec_boolean ("whole-system",
                          "Whole System",
                          "If the whole system should be profiled",
                          TRUE,
                          (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  signals [STOPPED] = g_signal_new ("stopped",
                                    G_TYPE_FROM_CLASS (klass),
                                    G_SIGNAL_RUN_LAST,
                                    G_STRUCT_OFFSET (SpProfilerClass, stopped),
                                    NULL, NULL, NULL, G_TYPE_NONE, 0);

  signals [FAILED] = g_signal_new ("failed",
                                    G_TYPE_FROM_CLASS (klass),
                                    G_SIGNAL_RUN_LAST,
                                    G_STRUCT_OFFSET (SpProfilerClass, failed),
                                    NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_ERROR);

}

static void
sp_profiler_init (SpProfiler *self)
{
  SpProfilerPrivate *priv = sp_profiler_get_instance_private (self);

  priv->whole_system = TRUE;

  priv->failures = g_ptr_array_new_with_free_func ((GDestroyNotify)g_error_free);
  priv->sources = g_ptr_array_new_with_free_func (g_object_unref);
  priv->starting = g_ptr_array_new_with_free_func (g_object_unref);
  priv->stopping = g_ptr_array_new_with_free_func (g_object_unref);
  priv->finished_or_failed = g_ptr_array_new_with_free_func (g_object_unref);
  priv->pids = g_array_new (FALSE, FALSE, sizeof (GPid));
}

SpProfiler *
sp_profiler_new (void)
{
  return g_object_new (SP_TYPE_PROFILER, NULL);
}

static void
sp_profiler_finish_startup (SpProfiler *self)
{
  SpProfilerPrivate *priv = sp_profiler_get_instance_private (self);
  guint i;

  g_assert (SP_IS_PROFILER (self));
  g_assert (priv->is_starting == TRUE);
  g_assert (priv->starting->len == 0);

  sp_profiler_clear_timer (self);

  priv->timer = g_timer_new ();

  /*
   * Add a source to update our watchers of elapsed time.
   * We use 1000 instead of add_seconds(1) so that we are
   * not subject to as much drift.
   */
  priv->timer_notify_source =
    g_timeout_add (1000,
                   sp_profiler_notify_elapsed_cb,
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
      g_signal_emit (self, signals [FAILED], 0, error);
      sp_profiler_stop (self);
      g_object_unref (self);
      return;
    }

  priv->is_running = TRUE;

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_IS_RUNNING]);
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_IS_MUTABLE]);

  /*
   * If all the sources are transient (in that they just generate information
   * and then exit), we could be finished as soon as we complete startup.
   *
   * If we detect this, we stop immediately.
   */
  if (priv->finished_or_failed->len == priv->sources->len)
    sp_profiler_stop (self);
}

void
sp_profiler_start (SpProfiler *self)
{
  SpProfilerPrivate *priv = sp_profiler_get_instance_private (self);
  guint i;

  g_return_if_fail (SP_IS_PROFILER (self));
  g_return_if_fail (priv->is_running == FALSE);
  g_return_if_fail (priv->is_stopping == FALSE);
  g_return_if_fail (priv->is_starting == FALSE);

  if (priv->writer == NULL)
    {
      SpCaptureWriter *writer;
      int fd;

      if ((-1 == (fd = syscall (__NR_memfd_create, "[sysprof]", 0))) ||
          (NULL == (writer = sp_capture_writer_new_from_fd (fd, 0))))
        {
          const GError error = {
            G_FILE_ERROR,
            g_file_error_from_errno (errno),
            (gchar *)g_strerror (errno)
          };

          if (fd != -1)
            close (fd);

          g_signal_emit (self, signals [FAILED], 0, &error);

          return;
        }

      sp_profiler_set_writer (self, writer);
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
    sp_profiler_finish_startup (self);
}

static void
sp_profiler_finish_stopping (SpProfiler *self)
{
  SpProfilerPrivate *priv = sp_profiler_get_instance_private (self);

  g_assert (SP_IS_PROFILER (self));
  g_assert (priv->is_starting == FALSE);
  g_assert (priv->is_stopping == TRUE);
  g_assert (priv->stopping->len == 0);

  if (priv->failures->len > 0)
    {
      const GError *error = g_ptr_array_index (priv->failures, 0);

      g_signal_emit (self, signals [FAILED], 0, error);
    }

  priv->is_running = FALSE;
  priv->is_stopping = FALSE;

  g_signal_emit (self, signals [STOPPED], 0);

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_IS_RUNNING]);
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_IS_MUTABLE]);
}

void
sp_profiler_stop (SpProfiler *self)
{
  SpProfilerPrivate *priv = sp_profiler_get_instance_private (self);
  guint i;

  g_return_if_fail (SP_IS_PROFILER (self));

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
    sp_profiler_finish_stopping (self);
}

gboolean
sp_profiler_get_is_running (SpProfiler *self)
{
  SpProfilerPrivate *priv = sp_profiler_get_instance_private (self);

  g_return_val_if_fail (SP_IS_PROFILER (self), FALSE);

  return priv->is_running;
}

void
sp_profiler_set_writer (SpProfiler      *self,
                        SpCaptureWriter *writer)
{
  SpProfilerPrivate *priv = sp_profiler_get_instance_private (self);

  g_return_if_fail (SP_IS_PROFILER (self));
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
sp_profiler_track_completed (SpProfiler *self,
                             SpSource   *source)
{
  SpProfilerPrivate *priv = sp_profiler_get_instance_private (self);
  gint i;

  g_assert (SP_IS_PROFILER (self));
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
            sp_profiler_finish_startup (self);
        }
    }

  if (priv->is_stopping)
    {
      i = _g_ptr_array_find (priv->stopping, source);

      if (i >= 0)
        {
          g_ptr_array_remove_index_fast (priv->stopping, i);

          if ((priv->is_stopping == TRUE) && (priv->stopping->len == 0))
            sp_profiler_finish_stopping (self);
        }
    }

  if (!priv->is_starting)
    {
      if (priv->finished_or_failed->len == priv->sources->len)
        sp_profiler_stop (self);
    }
}

static void
sp_profiler_source_finished (SpProfiler *self,
                             SpSource   *source)
{
  g_assert (SP_IS_PROFILER (self));
  g_assert (SP_IS_SOURCE (source));

  sp_profiler_track_completed (self, source);
}

static void
sp_profiler_source_ready (SpProfiler *self,
                          SpSource   *source)
{
  SpProfilerPrivate *priv = sp_profiler_get_instance_private (self);
  guint i;

  g_assert (SP_IS_PROFILER (self));
  g_assert (SP_IS_SOURCE (source));

  for (i = 0; i < priv->starting->len; i++)
    {
      SpSource *ele = g_ptr_array_index (priv->starting, i);

      if (ele == source)
        {
          g_ptr_array_remove_index_fast (priv->starting, i);

          if ((priv->is_starting == TRUE) && (priv->starting->len == 0))
            sp_profiler_finish_startup (self);

          break;
        }
    }
}

static void
sp_profiler_source_failed (SpProfiler   *self,
                           const GError *reason,
                           SpSource     *source)
{
  SpProfilerPrivate *priv = sp_profiler_get_instance_private (self);

  g_assert (SP_IS_PROFILER (self));
  g_assert (reason != NULL);
  g_assert (SP_IS_SOURCE (source));

  sp_profiler_track_completed (self, source);

  /* Failure emitted out of band */
  if (!priv->is_starting && !priv->is_stopping && !priv->is_running)
    return;

  g_ptr_array_add (priv->failures, g_error_copy (reason));

  /* Ignore during start/stop, we handle this in other places */
  if (priv->is_starting || priv->is_stopping)
    return;

  if (priv->is_running)
    sp_profiler_stop (self);
}

void
sp_profiler_add_source (SpProfiler *self,
                        SpSource   *source)
{
  SpProfilerPrivate *priv = sp_profiler_get_instance_private (self);

  g_return_if_fail (SP_IS_PROFILER (self));
  g_return_if_fail (SP_IS_SOURCE (source));
  g_return_if_fail (priv->is_running == FALSE);
  g_return_if_fail (priv->is_starting == FALSE);
  g_return_if_fail (priv->is_stopping == FALSE);

  g_signal_connect_object (source,
                           "failed",
                           G_CALLBACK (sp_profiler_source_failed),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (source,
                           "finished",
                           G_CALLBACK (sp_profiler_source_finished),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (source,
                           "ready",
                           G_CALLBACK (sp_profiler_source_ready),
                           self,
                           G_CONNECT_SWAPPED);


  g_ptr_array_add (priv->sources, g_object_ref (source));
}

void
sp_profiler_add_pid (SpProfiler *self,
                     GPid        pid)
{
  SpProfilerPrivate *priv = sp_profiler_get_instance_private (self);

  g_return_if_fail (SP_IS_PROFILER (self));
  g_return_if_fail (pid > -1);
  g_return_if_fail (priv->is_starting == FALSE);
  g_return_if_fail (priv->is_stopping == FALSE);
  g_return_if_fail (priv->is_running == FALSE);

  g_array_append_val (priv->pids, pid);
}

void
sp_profiler_remove_pid (SpProfiler *self,
                        GPid        pid)
{
  SpProfilerPrivate *priv = sp_profiler_get_instance_private (self);
  guint i;

  g_return_if_fail (SP_IS_PROFILER (self));
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

gboolean
sp_profiler_get_is_mutable (SpProfiler *self)
{
  SpProfilerPrivate *priv = sp_profiler_get_instance_private (self);

  g_return_val_if_fail (SP_IS_PROFILER (self), FALSE);

  return !(priv->is_starting || priv->is_stopping || priv->is_running);
}

gboolean
sp_profiler_get_whole_system (SpProfiler *self)
{
  SpProfilerPrivate *priv = sp_profiler_get_instance_private (self);

  g_return_val_if_fail (SP_IS_PROFILER (self), FALSE);

  return priv->whole_system;
}

void
sp_profiler_set_whole_system (SpProfiler *self,
                              gboolean    whole_system)
{
  SpProfilerPrivate *priv = sp_profiler_get_instance_private (self);

  g_return_if_fail (SP_IS_PROFILER (self));

  whole_system = !!whole_system;

  if (whole_system != priv->whole_system)
    {
      priv->whole_system = whole_system;
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_WHOLE_SYSTEM]);
    }
}

const GPid *
sp_profiler_get_pids (SpProfiler *self,
                      guint      *n_pids)
{
  SpProfilerPrivate *priv = sp_profiler_get_instance_private (self);

  g_return_val_if_fail (SP_IS_PROFILER (self), NULL);
  g_return_val_if_fail (n_pids != NULL, NULL);

  *n_pids = priv->pids->len;

  return (GPid *)(gpointer)priv->pids->data;
}

/**
 * sp_profiler_get_writer:
 *
 * Returns: (nullable) (transfer none): An #SpCaptureWriter or %NULL.
 */
SpCaptureWriter *
sp_profiler_get_writer (SpProfiler *self)
{
  SpProfilerPrivate *priv = sp_profiler_get_instance_private (self);

  g_return_val_if_fail (SP_IS_PROFILER (self), NULL);

  return priv->writer;
}

gboolean
sp_profiler_get_spawn (SpProfiler *self)
{
  SpProfilerPrivate *priv = sp_profiler_get_instance_private (self);

  g_return_val_if_fail (SP_IS_PROFILER (self), FALSE);

  return priv->spawn;
}

void
sp_profiler_set_spawn (SpProfiler *self,
                       gboolean    spawn)
{
  SpProfilerPrivate *priv = sp_profiler_get_instance_private (self);

  g_return_if_fail (SP_IS_PROFILER (self));

  spawn = !!spawn;

  if (priv->spawn != spawn)
    {
      priv->spawn = spawn;
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_SPAWN]);
    }
}

gboolean
sp_profiler_get_spawn_inherit_environ (SpProfiler *self)
{
  SpProfilerPrivate *priv = sp_profiler_get_instance_private (self);

  g_return_val_if_fail (SP_IS_PROFILER (self), FALSE);

  return priv->spawn_inherit_environ;
}

void
sp_profiler_set_spawn_inherit_environ (SpProfiler *self,
                                       gboolean    spawn_inherit_environ)
{
  SpProfilerPrivate *priv = sp_profiler_get_instance_private (self);

  g_return_if_fail (SP_IS_PROFILER (self));

  spawn_inherit_environ = !!spawn_inherit_environ;

  if (priv->spawn_inherit_environ != spawn_inherit_environ)
    {
      priv->spawn_inherit_environ = spawn_inherit_environ;
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_SPAWN_INHERIT_ENVIRON]);
    }
}

void
sp_profiler_set_spawn_argv (SpProfiler          *self,
                            const gchar * const *spawn_argv)
{
  SpProfilerPrivate *priv = sp_profiler_get_instance_private (self);

  g_return_if_fail (SP_IS_PROFILER (self));

  g_strfreev (priv->spawn_argv);
  priv->spawn_argv = g_strdupv ((gchar **)spawn_argv);
}

void
sp_profiler_set_spawn_env (SpProfiler          *self,
                           const gchar * const *spawn_env)
{
  SpProfilerPrivate *priv = sp_profiler_get_instance_private (self);

  g_return_if_fail (SP_IS_PROFILER (self));

  g_strfreev (priv->spawn_env);
  priv->spawn_env = g_strdupv ((gchar **)spawn_env);
}
