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
#include "sysprof-proc-source.h"

struct _SysprofProcSource
{
  GObject               parent_instance;
  SysprofCaptureWriter *writer;
  GArray               *pids;
};

static void    source_iface_init (SysprofSourceInterface *iface);
static gchar **proc_readlines    (const gchar       *format,
                                  ...)
  G_GNUC_PRINTF (1, 2);

G_DEFINE_TYPE_EXTENDED (SysprofProcSource, sysprof_proc_source, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (SYSPROF_TYPE_SOURCE, source_iface_init))

static gchar **
proc_readlines (const gchar *format,
                ...)
{
  SysprofHelpers *helpers = sysprof_helpers_get_default ();
  g_autofree gchar *filename = NULL;
  g_autofree gchar *contents = NULL;
  gchar **ret = NULL;
  va_list args;

  g_assert (format != NULL);

  va_start (args, format);
  filename = g_strdup_vprintf (format, args);
  va_end (args);

  if (sysprof_helpers_get_proc_file (helpers, filename, NULL, &contents, NULL))
    ret = g_strsplit (contents, "\n", 0);

  return g_steal_pointer (&ret);
}

gchar *
sysprof_proc_source_get_command_line (GPid      pid,
                                      gboolean *is_kernel)
{
  gchar *ret;
  gchar **lines;

  if (is_kernel)
    *is_kernel = FALSE;

  /*
   * Get the full command line from /proc/pid/cmdline.
   */
  if (NULL != (lines = proc_readlines ("/proc/%d/cmdline", pid)))
    {
      if (lines [0] && lines [0][0])
        {
          ret = g_strdup (lines [0]);
          g_strfreev (lines);
          return ret;
        }

      g_strfreev (lines);
    }

  /*
   * We are guessing this is a kernel process based on cmdline being null.
   */
  if (is_kernel)
    *is_kernel = TRUE;

  /*
   * Check the first line of /proc/pid/status for Name: foo
   */
  if (NULL != (lines = proc_readlines ("/proc/%d/status", pid)))
    {
      if (lines [0] && g_str_has_prefix (lines [0], "Name:"))
        {
          ret = g_strstrip (g_strdup (lines [0] + 5));
          g_strfreev (lines);
          return ret;
        }

      g_strfreev (lines);
    }

  return NULL;
}

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

static gboolean
strv_at_least_len (GStrv strv,
                   guint len)
{
  for (guint i = 0; i < len; i++)
    {
      if (strv[i] == NULL)
        return FALSE;
    }

  return TRUE;
}

static gchar *
find_mount (GStrv        mounts,
            const gchar *mount)
{
  gsize len = strlen (mount);

  for (guint i = 0; mounts[i]; i++)
    {
      const gchar *endptr;
      const gchar *begin;

      if (!g_str_has_prefix (mounts[i], mount))
        continue;

      if (mounts[i][len] != ' ')
        continue;

      begin = &mounts[i][len + 1];
      endptr = strchr (begin, ' ');
      if (endptr == NULL)
        continue;

      return g_strndup (begin, endptr - begin);
    }

  return NULL;
}

/**
 * sysprof_proc_source_translate_path:
 * @file: the path to the file inside target mount namespace
 * @mountinfo: mount info to locate translated path
 * @out_file: (out): location for the translated path
 * @out_file_len: length of @out_file
 *
 * This function will use @mountinfo to locate the longest common prefix
 * to determine which mount contains @file. That will be used to translate
 * the path pointed to by @file into the host namespace.
 *
 * The result is stored in @out_file and will always be NULL terminated.
 */
static void
sysprof_proc_source_translate_path (const gchar *file,
                                    GStrv        mountinfo,
                                    GStrv        mounts,
                                    gchar       *out_file,
                                    gsize        out_file_len)
{
  g_autofree gchar *closest_host = NULL;
  g_autofree gchar *closest_guest = NULL;
  g_autofree gchar *closest_mount = NULL;
  gsize closest_len = 0;

  g_assert (file != NULL);
  g_assert (g_str_has_prefix (file, "/newroot/"));
  g_assert (mountinfo != NULL);
  g_assert (out_file != NULL);

  if (!g_str_has_prefix (file, "/newroot/"))
    goto failure;

  file += strlen ("/newroot");

  for (guint i = 0; mountinfo[i] != NULL; i++)
    {
      g_auto(GStrv) parts = g_strsplit (mountinfo[i], " ", 11);
      const gchar *host;
      const gchar *guest;
      const gchar *mount;

      /*
       * Not ideal to do the string split here, but it is much easier
       * to just do that until we get this right, and then improve
       * things later when a strok()/etc parser.
       */

      if (!strv_at_least_len (parts, 10))
        continue;

      host = parts[3];
      guest = parts[4];
      mount = parts[9];

      if (g_str_has_prefix (file, guest))
        {
          gsize len = strlen (guest);

          if (len > closest_len && (file[len] == '\0' || file[len] == '/'))
            {
              g_free (closest_host);
              g_free (closest_guest);
              g_free (closest_mount);

              closest_guest = g_strdup (guest);
              closest_host = g_strdup (host);
              closest_mount = g_strdup (mount);

              closest_len = len;
            }
        }
    }

  if (closest_len > 0)
    {
      /*
       * The translated path is relative to the mount. So we need to add that
       * prefix to this as well, based on matching it from the closest_mount.
       */
      g_autofree gchar *mount = NULL;

      mount = find_mount (mounts, closest_mount);

      if (mount != NULL)
        {
          g_autofree gchar *path = NULL;

          path = g_build_filename (mount, closest_host, file + strlen (closest_guest), NULL);
          g_strlcpy (out_file, path, out_file_len);

          return;
        }
    }

failure:
  /* Fallback to just copying the source */
  g_strlcpy (out_file, file, out_file_len);
}

static void
sysprof_proc_source_populate_maps (SysprofProcSource *self,
                                   GPid               pid,
                                   GStrv              mounts,
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

  maps = g_strsplit (mapsstr, "\n", 0);
  mountinfo = g_strsplit (mountinfostr, "\n", 0);

  for (i = 0; maps [i] != NULL; i++)
    {
      gchar file[256];
      gchar translated[256];
      const gchar *fileptr = file;
      gulong start;
      gulong end;
      gulong offset;
      gulong inode;
      gint r;

      r = sscanf (maps [i],
                  "%lx-%lx %*15s %lx %*x:%*x %lu %255s",
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

      if (g_str_has_prefix (file, "/newroot/"))
        {
          /*
           * If this file starts with /newroot/, then it is in a different
           * mount-namespace from our profiler process. This means that we need
           * to translate the filename to the real path on disk inside our
           * (hopefully the default) mount-namespace. To do this, we have to
           * look at /proc/$pid/mountinfo to locate the longest-common-prefix
           * for the path.
           */
          sysprof_proc_source_translate_path (file,
                                              mountinfo,
                                              mounts,
                                              translated,
                                              sizeof translated);
          fileptr = translated;
        }

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
  g_auto(GStrv) mounts = NULL;
  gsize n_pids = 0;

  g_assert (SYSPROF_IS_PROC_SOURCE (self));
  g_assert (info != NULL);
  g_assert (g_variant_is_of_type (info, G_VARIANT_TYPE ("aa{sv}")));

  if (!(mounts = proc_readlines ("/proc/mounts")))
    return;

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
      sysprof_proc_source_populate_maps (self, pid, mounts, maps, mountinfo);

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
    }
  else
    {
      sysprof_proc_source_populate (self, info);
      sysprof_source_emit_finished (SYSPROF_SOURCE (self));
    }
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
