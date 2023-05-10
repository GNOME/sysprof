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

#include "sysprof-mount-namespace-private.h"

struct _SysprofMountNamespace
{
  GObject    parent_instance;
  GPtrArray *devices;
  GPtrArray *mounts;
};

G_DEFINE_FINAL_TYPE (SysprofMountNamespace, sysprof_mount_namespace, G_TYPE_OBJECT)

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
