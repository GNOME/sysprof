/* sysprof-mount-namespace.c
 *
 * Copyright 2023 Christian Hergert <chergert@redhat.com>
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

#include <gio/gio.h>

#include "timsort/gtktimsortprivate.h"

#include "sysprof-mount-namespace-private.h"

struct _SysprofMountNamespace
{
  GObject    parent_instance;
  GPtrArray *devices;
  GPtrArray *mounts;
  guint      mounts_dirty : 1;
};

static GType
sysprof_mount_namespace_get_item_type (GListModel *model)
{
  return SYSPROF_TYPE_MOUNT;
}

static guint
sysprof_mount_namespace_get_n_items (GListModel *model)
{
  return SYSPROF_MOUNT_NAMESPACE (model)->mounts->len;
}

static gpointer
sysprof_mount_namespace_get_item (GListModel *model,
                                  guint       position)
{
  SysprofMountNamespace *self = SYSPROF_MOUNT_NAMESPACE (model);

  if (position < self->mounts->len)
    return g_object_ref (g_ptr_array_index (self->mounts, position));

  return NULL;
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = sysprof_mount_namespace_get_item_type;
  iface->get_item = sysprof_mount_namespace_get_item;
  iface->get_n_items = sysprof_mount_namespace_get_n_items;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofMountNamespace, sysprof_mount_namespace, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

static void
sysprof_mount_namespace_finalize (GObject *object)
{
  SysprofMountNamespace *self = (SysprofMountNamespace *)object;

  g_clear_pointer (&self->devices, g_ptr_array_unref);
  g_clear_pointer (&self->mounts, g_ptr_array_unref);

  G_OBJECT_CLASS (sysprof_mount_namespace_parent_class)->finalize (object);
}

static void
sysprof_mount_namespace_class_init (SysprofMountNamespaceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_mount_namespace_finalize;
}

static void
sysprof_mount_namespace_init (SysprofMountNamespace *self)
{
  self->devices = g_ptr_array_new_with_free_func (g_object_unref);
  self->mounts = g_ptr_array_new_with_free_func (g_object_unref);
}

SysprofMountNamespace *
sysprof_mount_namespace_new (void)
{
  return g_object_new (SYSPROF_TYPE_MOUNT_NAMESPACE, NULL);
}

SysprofMountNamespace *
sysprof_mount_namespace_copy (SysprofMountNamespace *self)
{
  SysprofMountNamespace *copy;

  g_return_val_if_fail (SYSPROF_IS_MOUNT_NAMESPACE (self), NULL);

  copy = sysprof_mount_namespace_new ();

  for (guint i = 0; i < self->devices->len; i++)
    sysprof_mount_namespace_add_device (copy, g_object_ref (g_ptr_array_index (self->devices, i)));

  return copy;
}

/**
 * sysprof_mount_namespace_add_device:
 * @self: a #SysprofMountNamespace
 * @device: (transfer full): a #SysprofMountDevice
 *
 * Adds information about where a device is mounted on the host for resolving
 * paths to binaries.
 */
void
sysprof_mount_namespace_add_device (SysprofMountNamespace *self,
                                    SysprofMountDevice    *device)
{
  g_return_if_fail (SYSPROF_IS_MOUNT_NAMESPACE (self));
  g_return_if_fail (SYSPROF_IS_MOUNT_DEVICE (device));

  g_ptr_array_add (self->devices, device);
}

/**
 * sysprof_mount_namespace_add_mount:
 * @self: a #SysprofMountNamespace
 * @mount: (transfer full): a #SysprofMount
 *
 */
void
sysprof_mount_namespace_add_mount (SysprofMountNamespace *self,
                                   SysprofMount          *mount)
{
  g_return_if_fail (SYSPROF_IS_MOUNT_NAMESPACE (self));
  g_return_if_fail (SYSPROF_IS_MOUNT (mount));

  g_ptr_array_add (self->mounts, mount);

  self->mounts_dirty = TRUE;
}

static SysprofMountDevice *
sysprof_mount_namespace_find_device (SysprofMountNamespace *self,
                                     SysprofMount          *mount,
                                     const char            *relative_path)
{
  const char *mount_source;
  g_autofree char *subvolume = NULL;

  g_assert (SYSPROF_IS_MOUNT_NAMESPACE (self));
  g_assert (SYSPROF_IS_MOUNT (mount));

  while (relative_path[0] == '/')
    relative_path++;

  mount_source = sysprof_mount_get_mount_source (mount);
  subvolume = sysprof_mount_get_superblock_option (mount, "subvol");

  for (guint i = 0; i < self->devices->len; i++)
    {
      SysprofMountDevice *device = g_ptr_array_index (self->devices, i);
      const char *fs_spec = sysprof_mount_device_get_fs_spec (device);

      if (g_strcmp0 (fs_spec, mount_source) != 0)
        continue;

      if (subvolume != NULL)
        {
          const char *device_subvolume = sysprof_mount_device_get_subvolume (device);
          const char *mount_point = sysprof_mount_device_get_mount_point (device);

          if (g_strcmp0 (subvolume, device_subvolume) != 0)
            continue;

          /* Just ignore /sysroot, as it seems to be a convention on systems like
           * Silverblue or GNOME OS.
           */
          if (g_strcmp0 (mount_point, "/sysroot") == 0)
            continue;
        }

      return device;
    }

  return NULL;
}

static int
compare_mount (gconstpointer a,
               gconstpointer b)
{
  SysprofMount *mount_a = *(SysprofMount * const *)a;
  SysprofMount *mount_b = *(SysprofMount * const *)b;
  gsize alen = strlen (sysprof_mount_get_mount_point (mount_a));
  gsize blen = strlen (sysprof_mount_get_mount_point (mount_b));

  if (mount_a->is_overlay && !mount_b->is_overlay)
    return -1;
  else if (!mount_a->is_overlay && mount_b->is_overlay)
    return 1;

  if (alen > blen)
    return -1;
  else if (blen > alen)
    return 1;

  if (mount_a->layer < mount_b->layer)
    return -1;
  else if (mount_a->layer > mount_b->layer)
    return 1;

  return 0;
}

/**
 * sysprof_mount_namespace_translate:
 * @self: a #SysprofMountNamespace
 * @file: the path within the mount namespace to translate
 *
 * Attempts to translate a path within the mount namespace into a
 * path available in our current mount namespace.
 *
 * As overlays are involved, multiple paths may be returned which
 * could contain the target file. You should check these starting
 * from the first element in the resulting array to the last.
 *
 * Returns: (transfer full) (nullable): a UTF-8 encoded string array
 *   if successful; otherwise %NULL and @error is set.
 */
char **
sysprof_mount_namespace_translate (SysprofMountNamespace *self,
                                   const char            *file)
{
  g_autoptr(GArray) strv = NULL;

  g_return_val_if_fail (SYSPROF_IS_MOUNT_NAMESPACE (self), NULL);
  g_return_val_if_fail (file != NULL, NULL);

  if G_UNLIKELY (self->mounts_dirty)
    {
      gtk_tim_sort (self->mounts->pdata,
                    self->mounts->len,
                    sizeof (gpointer),
                    (GCompareDataFunc)compare_mount,
                    NULL);
      self->mounts_dirty = FALSE;
    }

  strv = g_array_new (TRUE, FALSE, sizeof (char *));

  for (guint i = 0; i < self->mounts->len; i++)
    {
      SysprofMount *mount = g_ptr_array_index (self->mounts, i);
      SysprofMountDevice *device;
      const char *device_mount_point;
      const char *fs_type;
      const char *relative;
      char *translated;

      if (!(relative = _sysprof_mount_get_relative_path (mount, file)))
        continue;

      fs_type = sysprof_mount_get_filesystem_type (mount);

      if (mount->is_overlay)
        {
          translated = g_build_filename (mount->mount_source, relative, NULL);
        }
      else if (g_strcmp0 (fs_type, "overlay") == 0)
        {
          g_autofree char *lowerdir_str = sysprof_mount_get_superblock_option (mount, "lowerdir");
          g_autofree char *upperdir_str = sysprof_mount_get_superblock_option (mount, "upperdir");
          g_auto(GStrv) lowerdirs = lowerdir_str ? g_strsplit (lowerdir_str, ":", 0) : NULL;
          g_auto(GStrv) upperdirs = upperdir_str ? g_strsplit (upperdir_str, ":", 0) : NULL;

          if (upperdirs != NULL)
            {
              for (guint j = 0; upperdirs[j]; j++)
                {
                  translated = g_build_filename (upperdirs[j], relative, NULL);
                  g_array_append_val (strv, translated);
                }
            }

          if (lowerdirs != NULL)
            {
              for (guint j = 0; lowerdirs[j]; j++)
                {
                  translated = g_build_filename (lowerdirs[j], relative, NULL);
                  g_array_append_val (strv, translated);
                }
            }

          continue;
        }
      else
        {
          const char *root;
          const char *subvolume;

          if (!(device = sysprof_mount_namespace_find_device (self, mount, relative)))
            continue;

          device_mount_point = sysprof_mount_device_get_mount_point (device);
          root = sysprof_mount_get_root (mount);
          subvolume = sysprof_mount_device_get_subvolume (device);

          if (root != NULL && subvolume != NULL)
            {
              if (g_strcmp0 (root, subvolume) == 0)
                root = "/";
              else if (g_str_has_prefix (root, subvolume) && root[strlen (subvolume)] == '/')
                root += strlen (subvolume);
            }

          translated = g_build_filename (device_mount_point, root, relative, NULL);
        }

      g_array_append_val (strv, translated);
    }

  if (strv->len == 0)
    {
      /* Try to passthrough the path in case we had no matches just to give
       * things a chance to decode. This can happen if we never recorded
       * the contents of /proc/$$/mountinfo.
       */
      char *copy = g_strdup (file);
      g_array_append_val (strv, copy);
    }

  return (char **)g_array_free (g_steal_pointer (&strv), FALSE);
}
