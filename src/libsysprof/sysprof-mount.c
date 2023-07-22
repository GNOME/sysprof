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

enum {
  PROP_0,
  PROP_DEVICE_MAJOR,
  PROP_DEVICE_MINOR,
  PROP_FILESYSTEM_TYPE,
  PROP_MOUNT_ID,
  PROP_MOUNT_POINT,
  PROP_MOUNT_SOURCE,
  PROP_PARENT_MOUNT_ID,
  PROP_ROOT,
  PROP_SUPERBLOCK_OPTIONS,
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

    case PROP_SUPERBLOCK_OPTIONS:
      g_value_set_string (value, sysprof_mount_get_superblock_options (self));
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

  properties[PROP_SUPERBLOCK_OPTIONS] =
    g_param_spec_string ("superblock-options", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_mount_init (SysprofMount *self)
{
}

SysprofMount *
_sysprof_mount_new_for_mountinfo (SysprofStrings *strings,
                                  const char     *mountinfo)
{
  g_autoptr(SysprofMount) self = NULL;
  g_auto(GStrv) parts = NULL;
  gsize n_parts;
  guint i;

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

  /* Skip forward to end of optional fields */
  for (i = 5; parts[i]; i++)
    {
      if (strcmp (parts[i], "-") == 0)
        break;
    }

  if (parts[i] == NULL)
    goto finish;

  /* filesystem-type column */
  i++;
  if (parts[i] == NULL)
    goto finish;
  self->filesystem_type = sysprof_strings_get (strings, parts[i]);

  /* mount-source column */
  i++;
  if (parts[i] == NULL)
    goto finish;
  self->mount_source = sysprof_strings_get (strings, parts[i]);

  /* superblock options column */
  i++;
  if (parts[i] == NULL)
    goto finish;
  self->superblock_options = sysprof_strings_get (strings, parts[i]);

finish:
  return g_steal_pointer (&self);
}

SysprofMount *
_sysprof_mount_new_for_overlay (SysprofStrings *strings,
                                const char     *mount_point,
                                const char     *host_path,
                                int             layer)
{
  SysprofMount *self;

  g_return_val_if_fail (strings != NULL, NULL);
  g_return_val_if_fail (mount_point != NULL, NULL);
  g_return_val_if_fail (host_path != NULL, NULL);

  self = g_object_new (SYSPROF_TYPE_MOUNT, NULL);
  self->mount_point = sysprof_strings_get (strings, mount_point);
  self->root = sysprof_strings_get (strings, "/");
  self->mount_source = sysprof_strings_get (strings, host_path);
  self->is_overlay = TRUE;

  return self;
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
sysprof_mount_get_superblock_options (SysprofMount *self)
{
  return self->superblock_options;
}

char *
sysprof_mount_get_superblock_option (SysprofMount *self,
                                     const char   *option)
{
  gsize option_len;

  g_return_val_if_fail (SYSPROF_IS_MOUNT (self), NULL);
  g_return_val_if_fail (option != NULL, NULL);

  if (self->superblock_options == NULL)
    return NULL;

  option_len = strlen (option);

  for (const char *c = strstr (self->superblock_options, option);
       c != NULL;
       c = strstr (c+1, option))
    {
      if ((c == self->superblock_options || c[-1] == ',') &&
          (c[option_len] == 0 || c[option_len] == '='))
        {
          const char *value;
          const char *endptr;

          if (c[option_len] == 0)
            return g_strdup ("");

          value = &c[option_len+1];

          if (!(endptr = strchr (value, ',')))
            return g_strdup (value);

          return g_strndup (value, endptr - value);
        }
    }

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

static inline gboolean
mount_is_root (SysprofMount *self)
{
  return self->mount_point[0] == '/' && self->mount_point[1] == 0;
}

const char *
_sysprof_mount_get_relative_path (SysprofMount *self,
                                  const char   *path)
{
  gsize len;

  g_return_val_if_fail (SYSPROF_IS_MOUNT (self), NULL);

  if (path == NULL || self->mount_point == NULL)
    return NULL;

  len = g_ref_string_length (self->mount_point);

  /* We don't care about directory paths, so ensure that we both
   * have the proper prefix and that it is in a subdirectory of this
   * mount point.
   */
  if (!mount_is_root (self) &&
      (!g_str_has_prefix (path, self->mount_point) || path[len] != '/'))
    return NULL;

  return &path[len];
}
