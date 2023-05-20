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

#include "sysprof-mount-namespace-private.h"

struct _SysprofMountNamespace
{
  GObject    parent_instance;
  GPtrArray *devices;
  GPtrArray *mounts;
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
}

void
sysprof_mount_namespace_add_overlay (SysprofMountNamespace  *self,
                                     SysprofDocumentOverlay *overlay)
{
  g_return_if_fail (SYSPROF_IS_MOUNT_NAMESPACE (self));
  g_return_if_fail (SYSPROF_IS_DOCUMENT_OVERLAY (overlay));

  /* TODO */
}

static SysprofMountDevice *
sysprof_mount_namespace_find_device (SysprofMountNamespace *self,
                                     SysprofMount          *mount,
                                     const char            *relative_path)
{
  const char *mount_source;
  const char *subvolume;

  g_assert (SYSPROF_IS_MOUNT_NAMESPACE (self));
  g_assert (SYSPROF_IS_MOUNT (mount));

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

          if (g_strcmp0 (subvolume, device_subvolume) != 0)
            continue;
        }

      return device;
    }

  return NULL;
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

  strv = g_array_new (TRUE, FALSE, sizeof (char *));

  for (guint i = 0; i < self->mounts->len; i++)
    {
      SysprofMount *mount = g_ptr_array_index (self->mounts, i);
      SysprofMountDevice *device;
      const char *device_mount_point;
      const char *relative;
      char *translated;

      if (!(relative = _sysprof_mount_get_relative_path (mount, file)) ||
          !(device = sysprof_mount_namespace_find_device (self, mount, relative)))
        continue;

      device_mount_point = sysprof_mount_device_get_mount_point (device);
      translated = g_build_filename (device_mount_point, relative, NULL);

      /* TODO: Still a bit to do here, because we need to handle overlays
       * still as a SysprofMount. Additionally, we may need to adjust the
       * paths a bit more based on subvolume, but I need a system such
       * as Silverblue or GNOME OS to test that again to match or improve
       * on existing behavior in libsysprof.
       */

      /* TODO: After we've translated to what we'd expect to see on the
       * host system, we'll need to translate once again to what we can
       * actually access if we're inside a container ourselves, such as
       * Toolbox or Flatpak and use /var/run/host or similar.
       */

      g_array_append_val (strv, translated);
    }

  if (strv->len == 0)
    return NULL;

  return (char **)g_array_free (g_steal_pointer (&strv), FALSE);
}
