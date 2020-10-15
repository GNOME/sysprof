/* sysprof-mountinfo.c
 *
 * Copyright 2019 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sysprof-mountinfo"

#include "config.h"

#include "sysprof-mountinfo.h"

typedef struct
{
  gchar *device;
  gchar *mountpoint;
  gchar *subvol;
} Mount;

typedef struct
{
  gchar *host_path;
  gchar *mount_path;
} Mountinfo;

struct _SysprofMountinfo
{
  GArray     *mounts;
  GArray     *mountinfos;
  GHashTable *dircache;
};

enum {
  COLUMN_MOUNT_ID = 0,
  COLUMN_MOUNT_PARENT_ID,
  COLUMN_MAJOR_MINOR,
  COLUMN_ROOT,
  COLUMN_MOUNT_POINT,
  COLUMN_MOUNT_OPTIONS,
};

static void
mount_clear (gpointer data)
{
  Mount *m = data;

  g_free (m->device);
  g_free (m->mountpoint);
  g_free (m->subvol);
}

static void
mountinfo_clear (gpointer data)
{
  Mountinfo *m = data;

  g_free (m->host_path);
  g_free (m->mount_path);
}

SysprofMountinfo *
sysprof_mountinfo_new (void)
{
  SysprofMountinfo *self;

  self = g_slice_new0 (SysprofMountinfo);
  self->mounts = g_array_new (FALSE, FALSE, sizeof (Mount));
  g_array_set_clear_func (self->mounts, mount_clear);
  self->mountinfos = g_array_new (FALSE, FALSE, sizeof (Mountinfo));
  g_array_set_clear_func (self->mountinfos, mountinfo_clear);
  self->dircache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

  return g_steal_pointer (&self);
}

void
sysprof_mountinfo_free (SysprofMountinfo *self)
{
  g_clear_pointer (&self->mounts, g_array_unref);
  g_clear_pointer (&self->mountinfos, g_array_unref);
  g_clear_pointer (&self->dircache, g_hash_table_unref);
  g_slice_free (SysprofMountinfo, self);
}

gchar *
sysprof_mountinfo_translate (SysprofMountinfo *self,
                             const gchar      *path)
{
  g_autofree gchar *dir = NULL;
  const gchar *translate;

  g_assert (self != NULL);

  if (path == NULL)
    return NULL;

  /* First try the dircache by looking up the translated parent
   * directory and appending basename to it.
   */
  dir = g_path_get_dirname (path);

  if ((translate = g_hash_table_lookup (self->dircache, dir)))
    {
      g_autofree gchar *name = g_path_get_basename (path);
      return g_build_filename (translate, name, NULL);
    }

  for (guint i = 0; i < self->mountinfos->len; i++)
    {
      const Mountinfo *m = &g_array_index (self->mountinfos, Mountinfo, i);

      if (g_str_has_prefix (path, m->mount_path))
        {
          gchar *ret;

          ret = g_build_filename (m->host_path, path + strlen (m->mount_path), NULL);
          g_hash_table_insert (self->dircache,
                               g_steal_pointer (&dir),
                               g_path_get_dirname (ret));
          return ret;
        }
    }

  return NULL;
}

static void
decode_space (gchar **str)
{
  /* Replace encoded space "\040" with ' ' */
  if (strstr (*str, "\\040"))
    {
      g_auto(GStrv) parts = g_strsplit (*str, "\\040", 0);
      g_free (*str);
      *str = g_strjoinv (" ", parts);
    }
}

void
sysprof_mountinfo_parse_mounts (SysprofMountinfo *self,
                                const gchar      *contents)
{
  g_auto(GStrv) lines = NULL;

  g_assert (self != NULL);
  g_assert (self->mounts != NULL);
  g_assert (contents != NULL);

  lines = g_strsplit (contents, "\n", 0);

  for (guint i = 0; lines[i]; i++)
    {
      g_auto(GStrv) parts = g_strsplit (lines[i], " ", 5);
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

      for (guint j = 0; parts[j]; j++)
        decode_space (&parts[j]);

      device = parts[0];
      mountpoint = parts[1];
      filesystem = parts[2];
      options = parts[3];

      if (g_strcmp0 (filesystem, "btrfs") == 0)
        {
          g_auto(GStrv) opts = g_strsplit (options, ",", 0);

          for (guint k = 0; opts[k]; k++)
            {
              if (g_str_has_prefix (opts[k], "subvol="))
                {
                  subvol = g_strdup (opts[k] + strlen ("subvol="));
                  break;
                }
            }
        }

      m.device = g_strdup (device);
      m.mountpoint = g_strdup (mountpoint);
      m.subvol = g_steal_pointer (&subvol);

      g_array_append_val (self->mounts, m);
    }
}

void
sysprof_mountinfo_reset (SysprofMountinfo *self)
{
  g_assert (self != NULL);
  g_assert (self->mountinfos != NULL);

  /* Keep mounts, but release mountinfos */
  if (self->mountinfos->len)
    g_array_remove_range (self->mountinfos, 0, self->mountinfos->len);
  g_hash_table_remove_all (self->dircache);
}

static const gchar *
get_device_mount (SysprofMountinfo *self,
                  const gchar      *device)
{
  g_assert (self != NULL);
  g_assert (self->mounts != NULL);
  g_assert (device != NULL);

  for (guint i = 0; i < self->mounts->len; i++)
    {
      const Mount *m = &g_array_index (self->mounts, Mount, i);

      if (strcmp (device, m->device) == 0)
        return m->mountpoint;
    }

  return NULL;
}

static void
sysprof_mountinfo_parse_mountinfo_line (SysprofMountinfo *self,
                                        const gchar      *line)
{
  g_auto(GStrv) parts = NULL;
  const gchar *prefix;
  const gchar *src;
  Mountinfo m;
  gsize n_parts;
  guint i;

  g_assert (self != NULL);
  g_assert (self->mounts != NULL);
  g_assert (self->mountinfos != NULL);

  parts = g_strsplit (line, " ", 0);
  n_parts = g_strv_length (parts);

  if (n_parts < 10)
    return;

  /* The device identifier is the 2nd column after "-" */
  for (i = 5; i < n_parts; i++)
    {
      if (strcmp (parts[i], "-") == 0)
        break;
    }
  if (i >= n_parts || parts[i][0] != '-' || parts[i+1] == NULL || parts[i+2] == NULL)
    return;

  prefix = get_device_mount (self, parts[i+2]);
  src = parts[COLUMN_ROOT];

  /* If this references a subvolume, try to find the mount by matching
   * the subvolumne using the "src". This isn't exactly correct, but it's
   * good enough to get btrfs stuff working for common installs.
   */
  if (g_strcmp0 (parts[8], "btrfs") == 0)
    {
      const char *subvol = src;

      for (i = 0; i < self->mounts->len; i++)
        {
          const Mount *mnt = &g_array_index (self->mounts, Mount, i);

          if (g_strcmp0 (mnt->subvol, subvol) == 0)
            {
              src = mnt->mountpoint;
              break;
            }
        }
    }

  while (*src == '/')
    src++;

  if (prefix != NULL)
    m.host_path = g_build_filename (prefix, src, NULL);
  else
    m.host_path = g_strdup (src);

  m.mount_path = g_strdup (parts[COLUMN_MOUNT_POINT]);
  g_array_append_val (self->mountinfos, m);
}

static gint
sort_by_length (gconstpointer a,
                gconstpointer b)
{
  const Mountinfo *mpa = a;
  const Mountinfo *mpb = b;
  gsize alen = strlen (mpa->mount_path);
  gsize blen = strlen (mpb->mount_path);

  if (alen > blen)
    return -1;
  else if (blen > alen)
    return 1;
  else
    return 0;
}

void
sysprof_mountinfo_parse_mountinfo (SysprofMountinfo *self,
                                   const gchar      *contents)
{
  g_auto(GStrv) lines = NULL;

  g_assert (self != NULL);
  g_assert (self->mounts != NULL);
  g_assert (self->mountinfos != NULL);

  lines = g_strsplit (contents, "\n", 0);

  for (guint i = 0; lines[i]; i++)
    sysprof_mountinfo_parse_mountinfo_line (self, lines[i]);

  g_array_sort (self->mountinfos, sort_by_length);
}
