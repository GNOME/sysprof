/* sp-gjs-source.c
 *
 * Copyright 2016 Christian Hergert <chergert@redhat.com>
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

#include <signal.h>
#include <string.h>

#include "sp-capture-reader.h"
#include "sp-gjs-source.h"

struct _SpGjsSource
{
  GObject          parent_instance;

  SpCaptureWriter *writer;
  GArray          *pids;
  GArray          *enabled;
};

#define ENABLE_PROFILER  0x1
#define DISABLE_PROFILER 0x0

static void source_iface_init (SpSourceInterface *iface);

G_DEFINE_TYPE_EXTENDED (SpGjsSource, sp_gjs_source, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (SP_TYPE_SOURCE, source_iface_init))

static void
sp_gjs_source_finalize (GObject *object)
{
  SpGjsSource *self = (SpGjsSource *)object;

  g_clear_pointer (&self->pids, g_array_unref);
  g_clear_pointer (&self->enabled, g_array_unref);
  g_clear_pointer (&self->writer, sp_capture_writer_unref);

  G_OBJECT_CLASS (sp_gjs_source_parent_class)->finalize (object);
}

static void
sp_gjs_source_class_init (SpGjsSourceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sp_gjs_source_finalize;
}

static void
sp_gjs_source_init (SpGjsSource *self)
{
  self->pids = g_array_new (FALSE, FALSE, sizeof (GPid));
  self->enabled = g_array_new (FALSE, FALSE, sizeof (GPid));
}

static void
sp_gjs_source_process_capture (SpGjsSource *self,
                               GPid         pid,
                               const gchar *path)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(SpCaptureReader) reader = NULL;

  g_assert (SP_IS_GJS_SOURCE (self));
  g_assert (self->writer != NULL);
  g_assert (path != NULL);

  if (!(reader = sp_capture_reader_new (path, &error)))
    {
      g_warning ("Failed to load capture: %s", error->message);
      return;
    }

  if (!sp_capture_reader_splice (reader, self->writer, &error))
    {
      g_warning ("Failed to load capture: %s", error->message);
      return;
    }
}

static void
sp_gjs_source_process_captures (SpGjsSource *self)
{
  guint i;

  g_assert (SP_IS_GJS_SOURCE (self));
  g_assert (self->writer != NULL);

  for (i = 0; i < self->enabled->len; i++)
    {
      g_autofree gchar *filename = NULL;
      g_autofree gchar *path = NULL;
      GPid pid = g_array_index (self->enabled, GPid, i);

      filename = g_strdup_printf ("gjs-profile-%u", (guint)pid);
      path = g_build_filename (g_get_tmp_dir (), filename, NULL);

      sp_gjs_source_process_capture (self, pid, path);
    }
}

static void
sp_gjs_source_set_writer (SpSource        *source,
                          SpCaptureWriter *writer)
{
  SpGjsSource *self = (SpGjsSource *)source;

  g_assert (SP_IS_GJS_SOURCE (self));
  g_assert (writer != NULL);

  self->writer = sp_capture_writer_ref (writer);
}

static gboolean
pid_is_profileable (GPid pid)
{
  g_autofree gchar *path = NULL;
  g_autofree gchar *contents = NULL;
  const gchar *libgjs;
  gsize len = 0;

  g_assert (pid != -1);

  /*
   * Make sure this process has linked in libgjs. No sense in sending it a
   * signal unless we know it can handle it.
   */

  path = g_strdup_printf ("/proc/%d/maps", pid);
  if (!g_file_get_contents (path, &contents, &len, NULL))
    return FALSE;

  if (NULL == (libgjs = strstr (contents, "libgjs."G_MODULE_SUFFIX)))
    return FALSE;

  return TRUE;
}

static void
sp_gjs_source_enable_pid (SpGjsSource *self,
                          GPid         pid)
{
  union sigval si;

  g_assert (SP_IS_GJS_SOURCE (self));
  g_assert (pid != -1);

  si.sival_int = ENABLE_PROFILER;

  if (0 != sigqueue (pid, SIGUSR2, si))
    g_warning ("Failed to queue SIGUSR2 to pid %u", (guint)pid);
  else
    g_array_append_val (self->enabled, pid);
}

static void
sp_gjs_source_disable_pid (SpGjsSource *self,
                           GPid         pid)
{
  union sigval si;

  g_assert (SP_IS_GJS_SOURCE (self));
  g_assert (pid != -1);

  si.sival_int = DISABLE_PROFILER;

  if (0 != sigqueue (pid, SIGUSR2, si))
    g_warning ("Failed to queue SIGUSR2 to pid %u", (guint)pid);
}

static void
sp_gjs_source_start (SpSource *source)
{
  SpGjsSource *self = (SpGjsSource *)source;
  guint i;

  g_assert (SP_IS_GJS_SOURCE (self));

  for (i = 0; i < self->pids->len; i++)
    {
      GPid pid = g_array_index (self->pids, GPid, i);

      if (pid_is_profileable (pid))
        sp_gjs_source_enable_pid (self, pid);
    }
}

static void
sp_gjs_source_stop (SpSource *source)
{
  SpGjsSource *self = (SpGjsSource *)source;
  guint i;

  g_assert (SP_IS_GJS_SOURCE (self));

  for (i = 0; i < self->pids->len; i++)
    {
      GPid pid = g_array_index (self->pids, GPid, i);

      if (pid_is_profileable (pid))
        sp_gjs_source_disable_pid (self, pid);
    }

  sp_gjs_source_process_captures (self);
}

static void
sp_gjs_source_add_pid (SpSource *source,
                       GPid      pid)
{
  SpGjsSource *self = (SpGjsSource *)source;

  g_assert (SP_IS_GJS_SOURCE (self));
  g_assert (pid > -1);

  g_array_append_val (self->pids, pid);
}

static void
source_iface_init (SpSourceInterface *iface)
{
  iface->set_writer = sp_gjs_source_set_writer;
  iface->start = sp_gjs_source_start;
  iface->stop = sp_gjs_source_stop;
  iface->add_pid = sp_gjs_source_add_pid;
}

SpSource *
sp_gjs_source_new (void)
{
  return g_object_new (SP_TYPE_GJS_SOURCE, NULL);
}
