/* sysprof-path-resolver.c
 *
 * Copyright 2021 Christian Hergert <chergert@redhat.com>
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

#include <string.h>

#include "sysprof-path-resolver.h"

struct _SysprofPathResolver
{
  GArray *mounts;
  GArray *mountpoints;
};

typedef struct
{
  /* The path on the host system */
  char *on_host;

  /* The path inside the process domain */
  char *in_process;

  /* The length of @in_process in bytes */
  guint in_process_len;

  /* The depth of the mount (for FUSE overlays) */
  int depth;
} Mountpoint;

typedef struct
{
  char *device;
  char *mountpoint;
  char *filesystem;
  char *subvolid;
  char *subvol;
} Mount;

typedef struct _st_mountinfo
{
  char *id;
  char *parent_id;
  char *st_dev;
  char *root;
  char *mount_point;
  char *mount_options;
  char *filesystem;
  char *mount_source;
  char *super_options;
} st_mountinfo;

static void
clear_st_mountinfo (st_mountinfo *st)
{
  g_free (st->id);
  g_free (st->parent_id);
  g_free (st->st_dev);
  g_free (st->root);
  g_free (st->mount_point);
  g_free (st->mount_options);
  g_free (st->filesystem);
  g_free (st->mount_source);
  g_free (st->super_options);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (st_mountinfo, clear_st_mountinfo)

static void
clear_mount (Mount *m)
{
  g_clear_pointer (&m->device, g_free);
  g_clear_pointer (&m->mountpoint, g_free);
  g_clear_pointer (&m->subvolid, g_free);
  g_clear_pointer (&m->subvol, g_free);
  g_clear_pointer (&m->filesystem, g_free);
}

static void
clear_mountpoint (Mountpoint *mp)
{
  g_clear_pointer (&mp->on_host, g_free);
  g_clear_pointer (&mp->in_process, g_free);
}

static gboolean
ignore_fs (const char *fs)
{
  static gsize initialized;
  static GHashTable *ignored;

  if (g_once_init_enter (&initialized))
    {
      ignored = g_hash_table_new (g_str_hash, g_str_equal);
      g_hash_table_add (ignored, (char *)"autofs");
      g_hash_table_add (ignored, (char *)"binfmt_misc");
      g_hash_table_add (ignored, (char *)"bpf");
      g_hash_table_add (ignored, (char *)"cgroup");
      g_hash_table_add (ignored, (char *)"cgroup2");
      g_hash_table_add (ignored, (char *)"configfs");
      g_hash_table_add (ignored, (char *)"debugfs");
      g_hash_table_add (ignored, (char *)"devpts");
      g_hash_table_add (ignored, (char *)"devtmpfs");
      g_hash_table_add (ignored, (char *)"efivarfs");
      g_hash_table_add (ignored, (char *)"fusectl");
      g_hash_table_add (ignored, (char *)"hugetlbfs");
      g_hash_table_add (ignored, (char *)"mqueue");
      g_hash_table_add (ignored, (char *)"none");
      g_hash_table_add (ignored, (char *)"portal");
      g_hash_table_add (ignored, (char *)"proc");
      g_hash_table_add (ignored, (char *)"pstore");
      g_hash_table_add (ignored, (char *)"ramfs");
      g_hash_table_add (ignored, (char *)"rpc_pipefs");
      g_hash_table_add (ignored, (char *)"securityfs");
      g_hash_table_add (ignored, (char *)"selinuxfs");
      g_hash_table_add (ignored, (char *)"sunrpc");
      g_hash_table_add (ignored, (char *)"sysfs");
      g_hash_table_add (ignored, (char *)"systemd-1");
      g_hash_table_add (ignored, (char *)"tmpfs");
      g_hash_table_add (ignored, (char *)"tracefs");
      g_once_init_leave (&initialized, (gsize)1);
    }

  if (g_str_has_prefix (fs, "fuse."))
    return TRUE;

  return g_hash_table_contains (ignored, fs);
}

static char *
path_copy_with_trailing_slash (const char *path)
{
  if (g_str_has_suffix (path, "/"))
    return g_strdup (path);
  else
    return g_strdup_printf ("%s/", path);
}

static void
decode_space (char **str)
{
  /* Replace encoded space "\040" with ' ' */
  if (strstr (*str, "\\040") != NULL)
    {
      g_auto(GStrv) parts = g_strsplit (*str, "\\040", 0);
      g_free (*str);
      *str = g_strjoinv (" ", parts);
    }
}

static char *
strdup_decode_space (const char *str)
{
  char *copy = g_strdup (str);
  decode_space (&copy);
  return copy;
}

static gboolean
has_prefix_or_equal (const char *str,
                     const char *prefix)
{
  return g_str_has_prefix (str, prefix) || strcmp (str, prefix) == 0;
}

static char *
get_option (const char *options,
            const char *option)
{
  g_auto(GStrv) parts = NULL;

  g_assert (option != NULL);
  g_assert (g_str_has_suffix (option, "="));

  if (options == NULL)
    return NULL;

  parts = g_strsplit (options, ",", 0);

  for (guint i = 0; parts[i] != NULL; i++)
    {
      if (g_str_has_prefix (parts[i], option))
        {
          const char *ret = parts[i] + strlen (option);

          /* Easier to handle "" as NULL */
          if (*ret == 0)
            return NULL;

          return g_strdup (ret);
        }
    }

  return NULL;
}

static gboolean
parse_st_mountinfo (st_mountinfo *mi,
                    const char   *line)
{
  g_auto(GStrv) parts = NULL;
  guint i;

  g_assert (mi != NULL);
  g_assert (line != NULL);

  memset (mi, 0, sizeof *mi);

  parts = g_strsplit (line, " ", 0);
  if (g_strv_length (parts) < 10)
    return FALSE;

  mi->id = g_strdup (parts[0]);
  mi->parent_id = g_strdup (parts[1]);
  mi->st_dev = g_strdup (parts[2]);
  mi->root = strdup_decode_space (parts[3]);
  mi->mount_point = strdup_decode_space (parts[4]);
  mi->mount_options = strdup_decode_space (parts[5]);

  for (i = 6; parts[i] != NULL && !g_str_equal (parts[i], "-"); i++)
    {
      /* Do nothing. We just want to skip until after the optional tags
       * section which is finished with " - ".
       */
    }

  /* Skip past - if there is anything */
  if (parts[i] == NULL || parts[++i] == NULL)
    return TRUE;

  /* Get filesystem if provided */
  mi->filesystem = g_strdup (parts[i++]);
  if (parts[i] == NULL)
    return TRUE;

  /* Get mount source if provided */
  mi->mount_source = strdup_decode_space (parts[i++]);
  if (parts[i] == NULL)
    return TRUE;

  /* Get super options if provided */
  mi->super_options = strdup_decode_space (parts[i++]);
  if (parts[i] == NULL)
    return TRUE;

  /* Perhaps mountinfo will be extended once again ... */

  return TRUE;
}

static void
parse_mounts (SysprofPathResolver *self,
              const char          *mounts)
{
  g_auto(GStrv) lines = NULL;

  g_assert (self != NULL);
  g_assert (self->mounts != NULL);
  g_assert (mounts != NULL);

  lines = g_strsplit (mounts, "\n", 0);

  for (guint i = 0; lines[i]; i++)
    {
      g_auto(GStrv) parts = g_strsplit (lines[i], " ", 5);
      g_autofree char *subvolid = NULL;
      g_autofree char *subvol = NULL;
      const char *filesystem;
      const char *mountpoint;
      const char *device;
      const char *options;
      Mount m;

      /* Field 0: device
       * Field 1: mountpoint
       * Field 2: filesystem
       * Field 3: Options
       * .. Ignored ..
       */
      if (g_strv_length (parts) != 5)
        continue;

      filesystem = parts[2];
      if (ignore_fs (filesystem))
        continue;

      for (guint j = 0; parts[j]; j++)
        decode_space (&parts[j]);

      device = parts[0];
      mountpoint = parts[1];
      options = parts[3];


      if (g_strcmp0 (filesystem, "btrfs") == 0)
        {
          subvolid = get_option (options, "subvolid=");
          subvol = get_option (options, "subvol=");
        }

      m.device = g_strdup (device);
      m.filesystem = g_strdup (filesystem);
      m.mountpoint = path_copy_with_trailing_slash (mountpoint);
      m.subvolid = g_steal_pointer (&subvolid);
      m.subvol = g_steal_pointer (&subvol);

      g_array_append_val (self->mounts, m);
    }
}

static const Mount *
find_mount (SysprofPathResolver *self,
            const st_mountinfo  *mi)
{
  g_autofree char *subvolid = NULL;

  g_assert (self != NULL);
  g_assert (mi != NULL);

  subvolid = get_option (mi->super_options, "subvolid=");

  for (guint i = 0; i < self->mounts->len; i++)
    {
      const Mount *mount = &g_array_index (self->mounts, Mount, i);

      if (g_strcmp0 (mount->device, mi->mount_source) == 0)
        {
          /* Sanity check that filesystems match */
          if (g_strcmp0 (mount->filesystem, mi->filesystem) != 0)
            continue;

          /* If we have a subvolume (btrfs) make sure they match */
          if (subvolid == NULL ||
              g_strcmp0 (subvolid, mount->subvolid) == 0)
            return mount;
        }
    }

  return NULL;
}

static void
parse_mountinfo_line (SysprofPathResolver *self,
                      const char          *line)
{
  g_auto(st_mountinfo) st_mi = {0};
  g_autofree char *subvol = NULL;
  const char *path;
  const Mount *mount;
  Mountpoint mp = {0};

  g_assert (self != NULL);
  g_assert (self->mounts != NULL);
  g_assert (self->mountpoints != NULL);

  if (!parse_st_mountinfo (&st_mi, line))
    return;

  if (ignore_fs (st_mi.filesystem))
    return;

  if (!(mount = find_mount (self, &st_mi)))
    return;

  subvol = get_option (st_mi.super_options, "subvol=");
  path = st_mi.root;

  /* If the mount root has a prefix of the subvolume, then
   * subtract that from the path (as we will resolve relative
   * to the location where it is mounted via the Mount.mountpoint.
   */
  if (subvol != NULL && has_prefix_or_equal (path, subvol))
    {
      path += strlen (subvol);
      if (*path == 0)
        path = NULL;
    }

  if (path == NULL)
    {
      mp.on_host = g_strdup (mount->mountpoint);
    }
  else
    {
      while (*path == '/')
        path++;
      mp.on_host = g_build_filename (mount->mountpoint, path, NULL);
    }

  if (g_str_has_suffix (mp.on_host, "/") &&
      !g_str_has_suffix (st_mi.mount_point, "/"))
    mp.in_process = g_build_filename (st_mi.mount_point, "/", NULL);
  else
    mp.in_process = g_strdup (st_mi.mount_point);

  mp.in_process_len = strlen (mp.in_process);
  mp.depth = -1;

  g_array_append_val (self->mountpoints, mp);
}

static gint
sort_by_length (gconstpointer a,
                gconstpointer b)
{
  const Mountpoint *mpa = a;
  const Mountpoint *mpb = b;
  gsize alen = strlen (mpa->in_process);
  gsize blen = strlen (mpb->in_process);

  if (alen > blen)
    return -1;
  else if (blen > alen)
    return 1;

  if (mpa->depth < mpb->depth)
    return -1;
  else if (mpa->depth > mpb->depth)
    return 1;

  return 0;
}

static void
parse_mountinfo (SysprofPathResolver *self,
                 const char          *mountinfo)
{
  g_auto(GStrv) lines = NULL;

  g_assert (self != NULL);
  g_assert (self->mounts != NULL);
  g_assert (self->mountpoints != NULL);
  g_assert (mountinfo != NULL);

  lines = g_strsplit (mountinfo, "\n", 0);

  for (guint i = 0; lines[i]; i++)
    parse_mountinfo_line (self, lines[i]);

  g_array_sort (self->mountpoints, sort_by_length);
}

SysprofPathResolver *
_sysprof_path_resolver_new (const char *mounts,
                            const char *mountinfo)
{
  SysprofPathResolver *self;

  self = g_slice_new0 (SysprofPathResolver);

  self->mounts = g_array_new (FALSE, FALSE, sizeof (Mount));
  self->mountpoints = g_array_new (FALSE, FALSE, sizeof (Mountpoint));

  g_array_set_clear_func (self->mounts, (GDestroyNotify)clear_mount);
  g_array_set_clear_func (self->mountpoints, (GDestroyNotify)clear_mountpoint);

  if (mounts != NULL)
    parse_mounts (self, mounts);

  if (mountinfo != NULL)
    parse_mountinfo (self, mountinfo);

  return self;
}

void
_sysprof_path_resolver_free (SysprofPathResolver *self)
{
  g_clear_pointer (&self->mountpoints, g_array_unref);
  g_clear_pointer (&self->mounts, g_array_unref);
  g_slice_free (SysprofPathResolver, self);
}

void
_sysprof_path_resolver_add_overlay (SysprofPathResolver *self,
                                    const char          *in_process,
                                    const char          *on_host,
                                    int                  depth)
{
  Mountpoint mp;

  g_return_if_fail (self != NULL);
  g_return_if_fail (in_process != NULL);
  g_return_if_fail (on_host != NULL);

  mp.in_process = path_copy_with_trailing_slash (in_process);
  mp.in_process_len = strlen (mp.in_process);
  mp.on_host = path_copy_with_trailing_slash (on_host);
  mp.depth = depth;

  g_array_append_val (self->mountpoints, mp);
  g_array_sort (self->mountpoints, sort_by_length);
}

char *
_sysprof_path_resolver_resolve (SysprofPathResolver *self,
                                const char          *path)
{
  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (path != NULL, NULL);

  /* TODO: Cache the directory name of @path and use that for followup
   *       searches like we did in SysprofMountinfo.
   */

  for (guint i = 0; i < self->mountpoints->len; i++)
    {
      const Mountpoint *mp = &g_array_index (self->mountpoints, Mountpoint, i);

      if (g_str_has_prefix (path, mp->in_process))
        {
          g_autofree char *dst = g_build_filename (mp->on_host,
                                                   path + mp->in_process_len,
                                                   NULL);

          /* If the depth is > -1, then we are dealing with an overlay. We
           * unfortunately have to stat() to see if the file exists and then
           * skip over this if not.
           *
           * TODO: This is going to break when we are recording from within
           *       flatpak as we'll not be able to stat files unless they are
           *       within the current users home, so system containers would
           *       be unlilkely to resolve.
           */
          if (mp->depth > -1)
            {
              if (g_file_test (dst, G_FILE_TEST_EXISTS))
                return g_steal_pointer (&dst);
              continue;
            }

          return g_steal_pointer (&dst);
        }
    }

  return NULL;
}
