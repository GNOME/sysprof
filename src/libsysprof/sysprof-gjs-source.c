/* sysprof-gjs-source.c
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

#include "config.h"

#include <signal.h>
#include <string.h>

#include "sysprof-capture-reader.h"
#include "sysprof-gjs-source.h"

struct _SysprofGjsSource
{
  GObject               parent_instance;

  SysprofCaptureWriter *writer;
  GArray               *pids;
  GArray               *enabled;
};

#define ENABLE_PROFILER  0x1
#define DISABLE_PROFILER 0x0

static void source_iface_init (SysprofSourceInterface *iface);

G_DEFINE_TYPE_EXTENDED (SysprofGjsSource, sysprof_gjs_source, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (SYSPROF_TYPE_SOURCE, source_iface_init))

static void
sysprof_gjs_source_finalize (GObject *object)
{
  SysprofGjsSource *self = (SysprofGjsSource *)object;

  g_clear_pointer (&self->pids, g_array_unref);
  g_clear_pointer (&self->enabled, g_array_unref);
  g_clear_pointer (&self->writer, sysprof_capture_writer_unref);

  G_OBJECT_CLASS (sysprof_gjs_source_parent_class)->finalize (object);
}

static void
sysprof_gjs_source_class_init (SysprofGjsSourceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_gjs_source_finalize;
}

static void
sysprof_gjs_source_init (SysprofGjsSource *self)
{
  self->pids = g_array_new (FALSE, FALSE, sizeof (GPid));
  self->enabled = g_array_new (FALSE, FALSE, sizeof (GPid));
}

static void
sysprof_gjs_source_process_capture (SysprofGjsSource *self,
                                    GPid              pid,
                                    const gchar      *path)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(SysprofCaptureReader) reader = NULL;

  g_assert (SYSPROF_IS_GJS_SOURCE (self));
  g_assert (self->writer != NULL);
  g_assert (path != NULL);

  if (!(reader = sysprof_capture_reader_new (path, &error)))
    {
      g_warning ("Failed to load capture: %s", error->message);
      return;
    }

  if (!sysprof_capture_reader_splice (reader, self->writer, &error))
    {
      g_warning ("Failed to load capture: %s", error->message);
      return;
    }
}

static void
sysprof_gjs_source_process_captures (SysprofGjsSource *self)
{
  guint i;

  g_assert (SYSPROF_IS_GJS_SOURCE (self));
  g_assert (self->writer != NULL);

  for (i = 0; i < self->enabled->len; i++)
    {
      g_autofree gchar *filename = NULL;
      g_autofree gchar *path = NULL;
      GPid pid = g_array_index (self->enabled, GPid, i);

      filename = g_strdup_printf ("gjs-profile-%u", (guint)pid);
      path = g_build_filename (g_get_tmp_dir (), filename, NULL);

      sysprof_gjs_source_process_capture (self, pid, path);
    }
}

static void
sysprof_gjs_source_set_writer (SysprofSource        *source,
                               SysprofCaptureWriter *writer)
{
  SysprofGjsSource *self = (SysprofGjsSource *)source;

  g_assert (SYSPROF_IS_GJS_SOURCE (self));
  g_assert (writer != NULL);

  self->writer = sysprof_capture_writer_ref (writer);
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
sysprof_gjs_source_enable_pid (SysprofGjsSource *self,
                               GPid              pid)
{
  union sigval si;

  g_assert (SYSPROF_IS_GJS_SOURCE (self));
  g_assert (pid != -1);

  si.sival_int = ENABLE_PROFILER;

  if (0 != sigqueue (pid, SIGUSR2, si))
    g_warning ("Failed to queue SIGUSR2 to pid %u", (guint)pid);
  else
    g_array_append_val (self->enabled, pid);
}

static void
sysprof_gjs_source_disable_pid (SysprofGjsSource *self,
                                GPid              pid)
{
  union sigval si;

  g_assert (SYSPROF_IS_GJS_SOURCE (self));
  g_assert (pid != -1);

  si.sival_int = DISABLE_PROFILER;

  if (0 != sigqueue (pid, SIGUSR2, si))
    g_warning ("Failed to queue SIGUSR2 to pid %u", (guint)pid);
}

static void
sysprof_gjs_source_start (SysprofSource *source)
{
  SysprofGjsSource *self = (SysprofGjsSource *)source;

  g_assert (SYSPROF_IS_GJS_SOURCE (self));

  for (guint i = 0; i < self->pids->len; i++)
    {
      GPid pid = g_array_index (self->pids, GPid, i);

      if (pid_is_profileable (pid))
        sysprof_gjs_source_enable_pid (self, pid);
    }
}

static void
sysprof_gjs_source_stop (SysprofSource *source)
{
  SysprofGjsSource *self = (SysprofGjsSource *)source;

  g_assert (SYSPROF_IS_GJS_SOURCE (self));

  for (guint i = 0; i < self->pids->len; i++)
    {
      GPid pid = g_array_index (self->pids, GPid, i);

      if (pid_is_profileable (pid))
        sysprof_gjs_source_disable_pid (self, pid);
    }

  sysprof_gjs_source_process_captures (self);
}

static void
sysprof_gjs_source_add_pid (SysprofSource *source,
                            GPid           pid)
{
  SysprofGjsSource *self = (SysprofGjsSource *)source;

  g_assert (SYSPROF_IS_GJS_SOURCE (self));
  g_assert (pid > -1);

  g_array_append_val (self->pids, pid);
}

static void
source_iface_init (SysprofSourceInterface *iface)
{
  iface->set_writer = sysprof_gjs_source_set_writer;
  iface->start = sysprof_gjs_source_start;
  iface->stop = sysprof_gjs_source_stop;
  iface->add_pid = sysprof_gjs_source_add_pid;
}

SysprofSource *
sysprof_gjs_source_new (void)
{
  return g_object_new (SYSPROF_TYPE_GJS_SOURCE, NULL);
}
