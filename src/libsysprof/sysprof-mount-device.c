/* sysprof-mount-device.c
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

#include "sysprof-mount-device-private.h"

struct _SysprofMountDevice
{
  GObject parent_instance;
  GRefString *fs_spec;
  GRefString *mount_point;
  GRefString *subvolume;
};

enum {
  PROP_0,
  PROP_FS_SPEC,
  PROP_MOUNT_POINT,
  PROP_SUBVOLUME,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofMountDevice, sysprof_mount_device, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static void
sysprof_mount_device_finalize (GObject *object)
{
  SysprofMountDevice *self = (SysprofMountDevice *)object;

  g_clear_pointer (&self->fs_spec, g_ref_string_release);
  g_clear_pointer (&self->mount_point, g_ref_string_release);
  g_clear_pointer (&self->subvolume, g_ref_string_release);

  G_OBJECT_CLASS (sysprof_mount_device_parent_class)->finalize (object);
}

static void
sysprof_mount_device_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  SysprofMountDevice *self = SYSPROF_MOUNT_DEVICE (object);

  switch (prop_id)
    {
    case PROP_FS_SPEC:
      g_value_set_string (value, sysprof_mount_device_get_fs_spec (self));
      break;

    case PROP_MOUNT_POINT:
      g_value_set_string (value, sysprof_mount_device_get_mount_point (self));
      break;

    case PROP_SUBVOLUME:
      g_value_set_string (value, sysprof_mount_device_get_subvolume (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_mount_device_class_init (SysprofMountDeviceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_mount_device_finalize;
  object_class->get_property = sysprof_mount_device_get_property;

  properties [PROP_FS_SPEC] =
    g_param_spec_string ("fs-spec", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_MOUNT_POINT] =
    g_param_spec_string ("mount-point", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SUBVOLUME] =
    g_param_spec_string ("subvolume", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_mount_device_init (SysprofMountDevice *self)
{
}

SysprofMountDevice *
sysprof_mount_device_new (GRefString *fs_spec,
                          GRefString *mount_point,
                          GRefString *subvolume)
{
  SysprofMountDevice *self;

  self = g_object_new (SYSPROF_TYPE_MOUNT_DEVICE, NULL);
  self->fs_spec = fs_spec;
  self->mount_point = mount_point;
  self->subvolume = subvolume;

  return g_steal_pointer (&self);
}

const char *
sysprof_mount_device_get_fs_spec (SysprofMountDevice *self)
{
  g_return_val_if_fail (SYSPROF_IS_MOUNT_DEVICE (self), NULL);

  return self->fs_spec;
}

const char *
sysprof_mount_device_get_mount_point (SysprofMountDevice *self)
{
  g_return_val_if_fail (SYSPROF_IS_MOUNT_DEVICE (self), NULL);

  return self->mount_point;
}

const char *
sysprof_mount_device_get_subvolume (SysprofMountDevice *self)
{
  g_return_val_if_fail (SYSPROF_IS_MOUNT_DEVICE (self), NULL);

  return self->subvolume;
}
