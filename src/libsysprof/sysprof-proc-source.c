/* sysprof-proc-source.c
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

/* Sysprof -- Sampling, systemwide CPU profiler
 * Copyright 2004, Red Hat, Inc.
 * Copyright 2004, 2005, Soeren Sandmann
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <json-glib/json-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sysprof-helpers.h"
#include "sysprof-podman.h"
#include "sysprof-proc-source.h"

struct _SysprofProcSource
{
  GObject               parent_instance;
  SysprofCaptureWriter *writer;
  GArray               *pids;
  SysprofPodman        *podman;
  guint                 is_ready : 1;
};

static void source_iface_init (SysprofSourceInterface *iface);

G_DEFINE_TYPE_EXTENDED (SysprofProcSource, sysprof_proc_source, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (SYSPROF_TYPE_SOURCE, source_iface_init))

static void
sysprof_proc_source_populate_process (SysprofProcSource *self,
                                      GPid               pid,
                                      const gchar       *cmdline)
{
  g_assert (SYSPROF_IS_PROC_SOURCE (self));
  g_assert (pid > 0);

  sysprof_capture_writer_add_process (self->writer,
                                      SYSPROF_CAPTURE_CURRENT_TIME,
                                      -1,
                                      pid,
                                      cmdline);
}

static void
sysprof_proc_source_populate_maps (SysprofProcSource *self,
                                   GPid               pid,
                                   const gchar       *mapsstr,
                                   gboolean           ignore_inode)
{
  g_auto(GStrv) lines = NULL;

  g_assert (SYSPROF_IS_PROC_SOURCE (self));
  g_assert (mapsstr != NULL);
  g_assert (pid > 0);

  lines = g_strsplit (mapsstr, "\n", 0);

  for (guint i = 0; lines[i] != NULL; i++)
    {
      gchar file[512];
      gulong start;
      gulong end;
      gulong offset;
      gulong inode;
      gint r;

      r = sscanf (lines[i],
                  "%lx-%lx %*15s %lx %*x:%*x %lu %511[^\n]",
                  &start, &end, &offset, &inode, file);
      file [sizeof file - 1] = '\0';

      /* file has a " (deleted)" suffix if it was deleted from disk */
      if (g_str_has_suffix (file, " (deleted)"))
          file [strlen (file) - strlen (" (deleted)")] = '\0';

      if (r != 5)
        continue;

      if (ignore_inode || strcmp ("[vdso]", file) == 0)
        {
          /*
           * Søren Sandmann Pedersen says:
           *
           * For the vdso, the kernel reports 'offset' as the
           * the same as the mapping addres. This doesn't make
           * any sense to me, so we just zero it here. There
           * is code in binfile.c (read_inode) that returns 0
           * for [vdso].
           */
          offset = 0;
          inode = 0;
        }

      sysprof_capture_writer_add_map (self->writer,
                                      SYSPROF_CAPTURE_CURRENT_TIME,
                                      -1,
                                      pid,
                                      start,
                                      end,
                                      offset,
                                      inode,
                                      file);
    }
}

static gboolean
pid_is_interesting (SysprofProcSource *self,
                    GPid               pid)
{
  if (self->pids == NULL || self->pids->len == 0)
    return TRUE;

  for (guint i = 0; i < self->pids->len; i++)
    {
      if (g_array_index (self->pids, GPid, i) == pid)
        return TRUE;
    }

  return FALSE;
}

static void
add_file (SysprofProcSource *self,
          int                pid,
          const char        *path,
          const char        *data)
{
  gsize to_write = strlen (data);

  while (to_write > 0)
    {
      gsize this_write = MIN (to_write, 4096 * 2);
      to_write -= this_write;
      sysprof_capture_writer_add_file (self->writer,
                                       SYSPROF_CAPTURE_CURRENT_TIME,
                                       -1,
                                       pid,
                                       path,
                                       to_write == 0,
                                       (const guint8 *)data,
                                       this_write);
      data += this_write;
    }
}

static void
sysprof_proc_source_populate_mountinfo (SysprofProcSource *self,
                                        GPid               pid,
                                        const char        *mountinfo)
{
  g_autofree char *path = NULL;

  g_assert (SYSPROF_IS_PROC_SOURCE (self));
  g_assert (self->writer != NULL);
  g_assert (mountinfo != NULL);

  path = g_strdup_printf ("/proc/%d/mountinfo", pid);
  add_file (self, pid, path, mountinfo);
}

static void
sysprof_proc_source_populate_pid_podman (SysprofProcSource *self,
                                         GPid               pid,
                                         const char        *container)
{
  g_auto(GStrv) layers = NULL;

  g_assert (SYSPROF_IS_PROC_SOURCE (self));
  g_assert (container != NULL);

  if (!(layers = sysprof_podman_get_layers (self->podman, container)))
    return;

  for (guint i = 0; layers[i]; i++)
    sysprof_capture_writer_add_overlay (self->writer,
                                        SYSPROF_CAPTURE_CURRENT_TIME,
                                        -1, pid, i, layers[i], "/");
}

static void
sysprof_proc_source_populate_overlays (SysprofProcSource *self,
                                       GPid               pid,
                                       const char        *cgroup)
{
  static GRegex *flatpak;
  static GRegex *podman;

  g_autoptr(GMatchInfo) flatpak_match = NULL;
  g_autoptr(GMatchInfo) podman_match = NULL;

  g_assert (SYSPROF_IS_PROC_SOURCE (self));
  g_assert (cgroup != NULL);

  if (strcmp (cgroup, "") == 0)
    return;

  /* This function tries to discover the podman container that contains the
   * process identified on the host as @pid. We can only do anything with this
   * if the pids are in containers that the running user controls (so that we
   * can actually access the overlays).
   *
   * This stuff, and I want to emphasize, is a giant hack. Just like containers
   * are on Linux. But if we are really careful, we can make this work for the
   * one particular use case I care about, which is podman/toolbox on Fedora
   * Workstation/Silverblue.
   *
   * -- Christian
   */
  if G_UNLIKELY (podman == NULL)
    {
      podman = g_regex_new ("libpod-([a-z0-9]{64})\\.scope", G_REGEX_OPTIMIZE, 0, NULL);
      g_assert (podman != NULL);
    }

  if (flatpak == NULL)
    {
      flatpak = g_regex_new ("app-flatpak-([a-zA-Z_\\-\\.]+)-[0-9]+\\.scope", G_REGEX_OPTIMIZE, 0, NULL);
      g_assert (flatpak != NULL);
    }

  if (g_regex_match (podman, cgroup, 0, &podman_match))
    {
      SysprofHelpers *helpers = sysprof_helpers_get_default ();
      g_autofree char *word = g_match_info_fetch (podman_match, 1);
      g_autofree char *path = g_strdup_printf ("/proc/%d/root/run/.containerenv", pid);
      g_autofree char *info = NULL;

      sysprof_proc_source_populate_pid_podman (self, pid, word);

      if (sysprof_helpers_get_proc_file (helpers, path, NULL, &info, NULL))
        add_file (self, pid, "/run/.containerenv", info);
    }
  else if (g_regex_match (flatpak, cgroup, 0, &flatpak_match))
    {
      SysprofHelpers *helpers = sysprof_helpers_get_default ();
      g_autofree char *word = g_match_info_fetch (flatpak_match, 1);
      g_autofree char *path = g_strdup_printf ("/proc/%d/root/.flatpak-info", pid);
      g_autofree char *info = NULL;

      if (sysprof_helpers_get_proc_file (helpers, path, NULL, &info, NULL))
        add_file (self, pid, "/.flatpak-info", info);
    }
}

static void
sysprof_proc_source_populate (SysprofProcSource *self,
                              GVariant          *info)
{
  g_autofree gchar *mounts = NULL;
  SysprofHelpers *helpers;
  gsize n_pids = 0;

  g_assert (SYSPROF_IS_PROC_SOURCE (self));
  g_assert (info != NULL);
  g_assert (g_variant_is_of_type (info, G_VARIANT_TYPE ("aa{sv}")));

  if (self->writer == NULL)
    return;

  if (self->podman == NULL)
    self->podman = sysprof_podman_snapshot_current_user ();

  helpers = sysprof_helpers_get_default ();
  if (!sysprof_helpers_get_proc_file (helpers, "/proc/mounts", NULL, &mounts, NULL))
    return;

  add_file (self, -1, "/proc/mounts", mounts);

  n_pids = g_variant_n_children (info);
  for (gsize i = 0; i < n_pids; i++)
    {
      g_autoptr(GVariant) pidinfo = g_variant_get_child_value (info, i);
      GVariantDict dict;
      const gchar *cmdline;
      const gchar *comm;
      const gchar *mountinfo;
      const gchar *maps;
      const gchar *cgroup;
      gboolean ignore_inode;
      gint32 pid;

      g_variant_dict_init (&dict, pidinfo);

      if (!g_variant_dict_lookup (&dict, "pid", "i", &pid) ||
          !pid_is_interesting (self, pid))
        goto skip;

      if (!g_variant_dict_lookup (&dict, "cmdline", "&s", &cmdline))
        cmdline = "";

      if (!g_variant_dict_lookup (&dict, "comm", "&s", &comm))
        comm = "";

      if (!g_variant_dict_lookup (&dict, "mountinfo", "&s", &mountinfo))
        mountinfo = "";

      if (!g_variant_dict_lookup (&dict, "maps", "&s", &maps))
        maps = "";

      if (!g_variant_dict_lookup (&dict, "cgroup", "&s", &cgroup))
        cgroup = "";

      /* Ignore inodes from podman/toolbox because they appear
       * to always be wrong. We'll have to rely on CRC instead.
       */
      ignore_inode = strstr (cgroup, "/libpod-") != NULL;

      sysprof_proc_source_populate_process (self, pid, *cmdline ? cmdline : comm);
      sysprof_proc_source_populate_mountinfo (self, pid, mountinfo);
      sysprof_proc_source_populate_maps (self, pid, maps, ignore_inode);
      sysprof_proc_source_populate_overlays (self, pid, cgroup);

      skip:
        g_variant_dict_clear (&dict);
    }
}

static void
sysprof_proc_source_get_process_info_cb (GObject      *object,
                                         GAsyncResult *result,
                                         gpointer      user_data)
{
  SysprofHelpers *helpers = (SysprofHelpers *)object;
  g_autoptr(SysprofProcSource) self = user_data;
  g_autoptr(GVariant) info = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (SYSPROF_IS_HELPERS (helpers));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (SYSPROF_IS_PROC_SOURCE (self));

  if (!sysprof_helpers_get_process_info_finish (helpers, result, &info, &error))
    {
      sysprof_source_emit_failed (SYSPROF_SOURCE (self), error);
      return;
    }

  sysprof_proc_source_populate (self, info);
  sysprof_source_emit_finished (SYSPROF_SOURCE (self));
}

static void
sysprof_proc_source_start (SysprofSource *source)
{
  SysprofProcSource *self = (SysprofProcSource *)source;
  SysprofHelpers *helpers = sysprof_helpers_get_default ();

  g_assert (SYSPROF_IS_PROC_SOURCE (self));
  g_assert (self->writer != NULL);

  sysprof_helpers_get_process_info_async (helpers,
                                          "pid,maps,mountinfo,cmdline,comm,cgroup",
                                          NULL,
                                          sysprof_proc_source_get_process_info_cb,
                                          g_object_ref (self));
}

static void
sysprof_proc_source_stop (SysprofSource *source)
{
  SysprofProcSource *self = (SysprofProcSource *)source;

  g_assert (SYSPROF_IS_PROC_SOURCE (self));

  g_clear_pointer (&self->writer, sysprof_capture_writer_unref);
}

static void
sysprof_proc_source_set_writer (SysprofSource        *source,
                                SysprofCaptureWriter *writer)
{
  SysprofProcSource *self = (SysprofProcSource *)source;

  g_assert (SYSPROF_IS_PROC_SOURCE (self));
  g_assert (writer != NULL);

  self->writer = sysprof_capture_writer_ref (writer);
}

static void
sysprof_proc_source_add_pid (SysprofSource *source,
                             GPid           pid)
{
  SysprofProcSource *self = (SysprofProcSource *)source;
  guint i;

  g_assert (SYSPROF_IS_PROC_SOURCE (self));
  g_assert (pid > -1);

  for (i = 0; i < self->pids->len; i++)
    {
      GPid ele = g_array_index (self->pids, GPid, i);

      if (ele == pid)
        return;
    }

  g_array_append_val (self->pids, pid);
}

static void
sysprof_proc_source_auth_cb (GObject      *object,
                             GAsyncResult *result,
                             gpointer      user_data)
{
  SysprofHelpers *helpers = (SysprofHelpers *)object;
  g_autoptr(SysprofProcSource) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (SYSPROF_IS_HELPERS (helpers));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (SYSPROF_IS_PROC_SOURCE (self));

  if (!sysprof_helpers_authorize_finish (helpers, result, &error))
    {
      sysprof_source_emit_failed (SYSPROF_SOURCE (self), error);
    }
  else
    {
      self->is_ready = TRUE;
      sysprof_source_emit_ready (SYSPROF_SOURCE (self));
    }
}

static void
sysprof_proc_source_prepare (SysprofSource *source)
{
  g_assert (SYSPROF_IS_PROC_SOURCE (source));

  sysprof_helpers_authorize_async (sysprof_helpers_get_default (),
                                   NULL,
                                   sysprof_proc_source_auth_cb,
                                   g_object_ref (source));
}

static gboolean
sysprof_proc_source_get_is_ready (SysprofSource *source)
{
  return SYSPROF_PROC_SOURCE (source)->is_ready;
}

static void
source_iface_init (SysprofSourceInterface *iface)
{
  iface->set_writer = sysprof_proc_source_set_writer;
  iface->start = sysprof_proc_source_start;
  iface->stop = sysprof_proc_source_stop;
  iface->add_pid = sysprof_proc_source_add_pid;
  iface->prepare = sysprof_proc_source_prepare;
  iface->get_is_ready = sysprof_proc_source_get_is_ready;
}

static void
sysprof_proc_source_finalize (GObject *object)
{
  SysprofProcSource *self = (SysprofProcSource *)object;

  g_clear_pointer (&self->writer, sysprof_capture_writer_unref);
  g_clear_pointer (&self->pids, g_array_unref);
  g_clear_pointer (&self->podman, sysprof_podman_free);

  G_OBJECT_CLASS (sysprof_proc_source_parent_class)->finalize (object);
}

static void
sysprof_proc_source_class_init (SysprofProcSourceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_proc_source_finalize;
}

static void
sysprof_proc_source_init (SysprofProcSource *self)
{
  self->pids = g_array_new (FALSE, FALSE, sizeof (GPid));
}

SysprofSource *
sysprof_proc_source_new (void)
{
  return g_object_new (SYSPROF_TYPE_PROC_SOURCE, NULL);
}
