/* sysprof-mount.c
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

#include <stdio.h>

#include "sysprof-mount-private.h"

struct _SysprofMount
{
  GObject parent_instance;
  int mount_id;
  int parent_mount_id;
  int device_major;
  int device_minor;
  GRefString *root;
  GRefString *mount_point;
  GRefString *mount_source;
  GRefString *filesystem_type;
  GRefString *superblock_options;
};

enum {
  PROP_0,
  PROP_DEVICE_MAJOR,
  PROP_DEVICE_MINOR,
  PROP_ROOT,
  PROP_MOUNT_ID,
  PROP_MOUNT_POINT,
  PROP_MOUNT_SOURCE,
  PROP_PARENT_MOUNT_ID,
  PROP_FILESYSTEM_TYPE,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofMount, sysprof_mount, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static void
sysprof_mount_finalize (GObject *object)
{
  SysprofMount *self = (SysprofMount *)object;

  g_clear_pointer (&self->root, g_ref_string_release);
  g_clear_pointer (&self->mount_point, g_ref_string_release);
  g_clear_pointer (&self->mount_source, g_ref_string_release);
  g_clear_pointer (&self->filesystem_type, g_ref_string_release);
  g_clear_pointer (&self->superblock_options, g_ref_string_release);

  G_OBJECT_CLASS (sysprof_mount_parent_class)->finalize (object);
}

static void
sysprof_mount_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  SysprofMount *self = SYSPROF_MOUNT (object);

  switch (prop_id)
    {
    case PROP_DEVICE_MAJOR:
      g_value_set_int (value, sysprof_mount_get_device_major (self));
      break;

    case PROP_DEVICE_MINOR:
      g_value_set_int (value, sysprof_mount_get_device_minor (self));
      break;

    case PROP_ROOT:
      g_value_set_string (value, sysprof_mount_get_root (self));
      break;

    case PROP_MOUNT_ID:
      g_value_set_int (value, sysprof_mount_get_mount_id (self));
      break;

    case PROP_MOUNT_POINT:
      g_value_set_string (value, sysprof_mount_get_mount_point (self));
      break;

    case PROP_MOUNT_SOURCE:
      g_value_set_string (value, sysprof_mount_get_mount_source (self));
      break;

    case PROP_PARENT_MOUNT_ID:
      g_value_set_int (value, sysprof_mount_get_parent_mount_id (self));
      break;

    case PROP_FILESYSTEM_TYPE:
      g_value_set_string (value, sysprof_mount_get_filesystem_type (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_mount_class_init (SysprofMountClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_mount_finalize;
  object_class->get_property = sysprof_mount_get_property;

  properties[PROP_DEVICE_MAJOR] =
    g_param_spec_int ("device-major", NULL, NULL,
                      G_MININT, G_MAXINT, 0,
                      (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_DEVICE_MINOR] =
    g_param_spec_int ("device-minor", NULL, NULL,
                      G_MININT, G_MAXINT, 0,
                      (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_ROOT] =
    g_param_spec_string ("root", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_MOUNT_ID] =
    g_param_spec_int ("mount-id", NULL, NULL,
                      G_MININT, G_MAXINT, 0,
                      (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_PARENT_MOUNT_ID] =
    g_param_spec_int ("parent-mount-id", NULL, NULL,
                      G_MININT, G_MAXINT, 0,
                      (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_MOUNT_POINT] =
    g_param_spec_string ("mount-point", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_MOUNT_SOURCE] =
    g_param_spec_string ("mount-source", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_FILESYSTEM_TYPE] =
    g_param_spec_string ("filesystem-type", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_mount_init (SysprofMount *self)
{
}

SysprofMount *
sysprof_mount_new_for_mountinfo (SysprofStrings *strings,
                                 const char     *mountinfo)
{
  g_autoptr(SysprofMount) self = NULL;
  g_auto(GStrv) parts = NULL;
  gsize n_parts;

  g_return_val_if_fail (strings != NULL, NULL);
  g_return_val_if_fail (mountinfo != NULL, NULL);

  parts = g_strsplit (mountinfo, " ", 20);
  n_parts = g_strv_length (parts);
  if (n_parts < 10)
    return NULL;

  self = g_object_new (SYSPROF_TYPE_MOUNT, NULL);

  self->mount_id = g_ascii_strtoll (parts[0], NULL, 10);
  self->parent_mount_id = g_ascii_strtoll (parts[1], NULL, 10);
  sscanf (parts[2], "%d:%d", &self->device_major, &self->device_minor);
  self->root = sysprof_strings_get (strings, parts[3]);
  self->mount_point = sysprof_strings_get (strings, parts[4]);

  return g_steal_pointer (&self);
}

int
sysprof_mount_get_device_major (SysprofMount *self)
{
  return self->device_major;
}

int
sysprof_mount_get_device_minor (SysprofMount *self)
{
  return self->device_minor;
}

const char *
sysprof_mount_get_root (SysprofMount *self)
{
  return self->root;
}

const char *
sysprof_mount_get_mount_point (SysprofMount *self)
{
  return self->mount_point;
}

const char *
sysprof_mount_get_mount_source (SysprofMount *self)
{
  return self->mount_source;
}

const char *
sysprof_mount_get_filesystem_type (SysprofMount *self)
{
  return self->filesystem_type;
}

const char *
sysprof_mount_get_superblock_option (SysprofMount *self,
                                     const char   *option)
{
  return NULL;
}

int
sysprof_mount_get_mount_id (SysprofMount *self)
{
  return self->mount_id;
}

int
sysprof_mount_get_parent_mount_id (SysprofMount *self)
{
  return self->parent_mount_id;
}
