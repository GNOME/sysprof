/* sp-proc-source.c
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sp-proc-source.h"

struct _SpProcSource
{
  GObject          parent_instance;
  SpCaptureWriter *writer;
  GArray          *pids;
};

static void    source_iface_init (SpSourceInterface *iface);
static gchar **proc_readlines    (const gchar       *format,
                                  ...)
  G_GNUC_PRINTF (1, 2);

G_DEFINE_TYPE_EXTENDED (SpProcSource, sp_proc_source, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (SP_TYPE_SOURCE, source_iface_init))

static gchar **
proc_readlines (const gchar *format,
                ...)
{
  gchar **ret = NULL;
  gchar *filename = NULL;
  gchar *contents = NULL;
  va_list args;
  gsize len;

  g_assert (format != NULL);

  va_start (args, format);
  filename = g_strdup_vprintf (format, args);
  va_end (args);

  if (g_file_get_contents (filename, &contents, &len, NULL))
    ret = g_strsplit (contents, "\n", 0);

  g_free (contents);
  g_free (filename);

  return ret;
}

gchar *
sp_proc_source_get_command_line (GPid      pid,
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
sp_proc_source_populate_process (SpProcSource *self,
                                 GPid          pid)
{
  gchar *cmdline;

  g_assert (SP_IS_PROC_SOURCE (self));
  g_assert (pid > 0);

  if (NULL != (cmdline = sp_proc_source_get_command_line (pid, NULL)))
    {
      sp_capture_writer_add_process (self->writer,
                                     SP_CAPTURE_CURRENT_TIME,
                                     -1,
                                     pid,
                                     cmdline);
      g_free (cmdline);
    }
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
 * sp_proc_source_translate_path:
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
sp_proc_source_translate_path (const gchar *file,
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
sp_proc_source_populate_maps (SpProcSource *self,
                              GPid          pid,
                              GStrv         mounts)
{
  g_auto(GStrv) lines = NULL;
  g_auto(GStrv) mountinfo = NULL;
  guint i;

  g_assert (SP_IS_PROC_SOURCE (self));
  g_assert (pid > 0);

  if (NULL == (lines = proc_readlines ("/proc/%d/maps", pid)))
    return;

  if (NULL == (mountinfo = proc_readlines ("/proc/%d/mountinfo", pid)))
    return;

  for (i = 0; lines [i] != NULL; i++)
    {
      gchar file[256];
      gchar translated[256];
      const gchar *fileptr = file;
      gulong start;
      gulong end;
      gulong offset;
      gulong inode;
      gint r;

      r = sscanf (lines [i],
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
          sp_proc_source_translate_path (file,
                                         mountinfo,
                                         mounts,
                                         translated,
                                         sizeof translated);
          fileptr = translated;
        }

      sp_capture_writer_add_map (self->writer,
                                 SP_CAPTURE_CURRENT_TIME,
                                 -1,
                                 pid,
                                 start,
                                 end,
                                 offset,
                                 inode,
                                 fileptr);
    }
}

static void
sp_proc_source_populate (SpProcSource *self)
{
  g_auto(GStrv) mounts = NULL;
  const gchar *name;
  GDir *dir;

  g_assert (SP_IS_PROC_SOURCE (self));

  if (NULL == (mounts = proc_readlines ("/proc/mounts")))
    return;

  if (self->pids->len > 0)
    {
      guint i;

      for (i = 0; i < self->pids->len; i++)
        {
          GPid pid = g_array_index (self->pids, GPid, i);

          sp_proc_source_populate_process (self, pid);
          sp_proc_source_populate_maps (self, pid, mounts);
        }

      return;
    }

  if (NULL == (dir = g_dir_open ("/proc", 0, NULL)))
    return;

  while (NULL != (name = g_dir_read_name (dir)))
    {
      GPid pid;
      char *end;

      pid = strtol (name, &end, 10);
      if (pid <= 0 || *end != '\0')
        continue;

      sp_proc_source_populate_process (self, pid);
      sp_proc_source_populate_maps (self, pid, mounts);
    }

  g_dir_close (dir);
}

static void
sp_proc_source_start (SpSource *source)
{
  SpProcSource *self = (SpProcSource *)source;

  g_assert (SP_IS_PROC_SOURCE (self));
  g_assert (self->writer != NULL);

  sp_proc_source_populate (self);
  sp_source_emit_finished (source);
}

static void
sp_proc_source_stop (SpSource *source)
{
  SpProcSource *self = (SpProcSource *)source;

  g_assert (SP_IS_PROC_SOURCE (self));

  g_clear_pointer (&self->writer, sp_capture_writer_unref);
}

static void
sp_proc_source_set_writer (SpSource        *source,
                           SpCaptureWriter *writer)
{
  SpProcSource *self = (SpProcSource *)source;

  g_assert (SP_IS_PROC_SOURCE (self));
  g_assert (writer != NULL);

  self->writer = sp_capture_writer_ref (writer);
}

static void
sp_proc_source_add_pid (SpSource *source,
                        GPid      pid)
{
  SpProcSource *self = (SpProcSource *)source;
  guint i;

  g_assert (SP_IS_PROC_SOURCE (self));
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
source_iface_init (SpSourceInterface *iface)
{
  iface->set_writer = sp_proc_source_set_writer;
  iface->start = sp_proc_source_start;
  iface->stop = sp_proc_source_stop;
  iface->add_pid = sp_proc_source_add_pid;
}

static void
sp_proc_source_finalize (GObject *object)
{
  SpProcSource *self = (SpProcSource *)object;

  g_clear_pointer (&self->writer, sp_capture_writer_unref);
  g_clear_pointer (&self->pids, g_array_unref);

  G_OBJECT_CLASS (sp_proc_source_parent_class)->finalize (object);
}

static void
sp_proc_source_class_init (SpProcSourceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sp_proc_source_finalize;
}

static void
sp_proc_source_init (SpProcSource *self)
{
  self->pids = g_array_new (FALSE, FALSE, sizeof (GPid));
}

SpSource *
sp_proc_source_new (void)
{
  return g_object_new (SP_TYPE_PROC_SOURCE, NULL);
}
