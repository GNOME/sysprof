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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sysprof-helpers.h"
#include "sysprof-mountinfo.h"
#include "sysprof-proc-source.h"

struct _SysprofProcSource
{
  GObject               parent_instance;
  SysprofCaptureWriter *writer;
  GArray               *pids;
  SysprofMountinfo     *mountinfo;
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
                                   const gchar       *mountinfostr)
{
  g_auto(GStrv) maps = NULL;
  g_auto(GStrv) mountinfo = NULL;
  guint i;

  g_assert (SYSPROF_IS_PROC_SOURCE (self));
  g_assert (mapsstr != NULL);
  g_assert (mountinfostr != NULL);
  g_assert (pid > 0);

  sysprof_mountinfo_reset (self->mountinfo);
  sysprof_mountinfo_parse_mountinfo (self->mountinfo, mountinfostr);

  maps = g_strsplit (mapsstr, "\n", 0);

  for (i = 0; maps [i] != NULL; i++)
    {
      g_autofree gchar *translated = NULL;
      gchar file[512];
      const gchar *fileptr = file;
      gulong start;
      gulong end;
      gulong offset;
      gulong inode;
      gint r;

      r = sscanf (maps [i],
                  "%lx-%lx %*15s %lx %*x:%*x %lu %512s",
                  &start, &end, &offset, &inode, file);
      file [sizeof file - 1] = '\0';

      if (r != 5)
        continue;

      if (strcmp ("[vdso]", file) == 0)
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

      /* Possibly translate the path based on mounts/mountinfo data */
      if ((translated = sysprof_mountinfo_translate (self->mountinfo, file)))
        fileptr = translated;

      sysprof_capture_writer_add_map (self->writer,
                                      SYSPROF_CAPTURE_CURRENT_TIME,
                                      -1,
                                      pid,
                                      start,
                                      end,
                                      offset,
                                      inode,
                                      fileptr);
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

  helpers = sysprof_helpers_get_default ();
  if (!sysprof_helpers_get_proc_file (helpers, "/proc/mounts", NULL, &mounts, NULL))
    return;

  sysprof_mountinfo_parse_mounts (self->mountinfo, mounts);

  n_pids = g_variant_n_children (info);
  for (gsize i = 0; i < n_pids; i++)
    {
      g_autoptr(GVariant) pidinfo = g_variant_get_child_value (info, i);
      GVariantDict dict;
      const gchar *cmdline;
      const gchar *comm;
      const gchar *mountinfo;
      const gchar *maps;
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

      sysprof_proc_source_populate_process (self, pid, *cmdline ? cmdline : comm);
      sysprof_proc_source_populate_maps (self, pid, maps, mountinfo);

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
                                          "pid,maps,mountinfo,cmdline,comm",
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
source_iface_init (SysprofSourceInterface *iface)
{
  iface->set_writer = sysprof_proc_source_set_writer;
  iface->start = sysprof_proc_source_start;
  iface->stop = sysprof_proc_source_stop;
  iface->add_pid = sysprof_proc_source_add_pid;
}

static void
sysprof_proc_source_finalize (GObject *object)
{
  SysprofProcSource *self = (SysprofProcSource *)object;

  g_clear_pointer (&self->writer, sysprof_capture_writer_unref);
  g_clear_pointer (&self->pids, g_array_unref);
  g_clear_pointer (&self->mountinfo, sysprof_mountinfo_free);

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
  self->mountinfo = sysprof_mountinfo_new ();
}

SysprofSource *
sysprof_proc_source_new (void)
{
  return g_object_new (SYSPROF_TYPE_PROC_SOURCE, NULL);
}
