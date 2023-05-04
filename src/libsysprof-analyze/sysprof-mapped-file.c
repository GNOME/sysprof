/* sysprof-mapped-file.c
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

#include "sysprof-mapped-file-private.h"

struct _SysprofMappedFile
{
  GObject  parent_instance;
  char    *path;
  guint64  address;
  guint64  offset;
  guint64  length;
  gint64   inode;
};

enum {
  PROP_0,
  PROP_ADDRESS,
  PROP_INODE,
  PROP_LENGTH,
  PROP_OFFSET,
  PROP_PATH,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofMappedFile, sysprof_mapped_file, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static void
sysprof_mapped_file_finalize (GObject *object)
{
  SysprofMappedFile *self = (SysprofMappedFile *)object;

  g_clear_pointer (&self->path, g_ref_string_release);

  G_OBJECT_CLASS (sysprof_mapped_file_parent_class)->finalize (object);
}

static void
sysprof_mapped_file_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  SysprofMappedFile *self = SYSPROF_MAPPED_FILE (object);

  switch (prop_id)
    {
    case PROP_ADDRESS:
      g_value_set_uint64 (value, self->address);
      break;

    case PROP_INODE:
      g_value_set_int64 (value, self->inode);
      break;

    case PROP_LENGTH:
      g_value_set_uint64 (value, self->length);
      break;

    case PROP_OFFSET:
      g_value_set_uint64 (value, self->offset);
      break;

    case PROP_PATH:
      g_value_set_string (value, self->path);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_mapped_file_class_init (SysprofMappedFileClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_mapped_file_finalize;
  object_class->get_property = sysprof_mapped_file_get_property;

  properties [PROP_ADDRESS] =
    g_param_spec_uint64 ("address", NULL, NULL,
                         0, G_MAXUINT64, 0,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_INODE] =
    g_param_spec_int64 ("inode", NULL, NULL,
                        G_MININT64, G_MAXINT64, 0,
                        (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_LENGTH] =
    g_param_spec_uint64 ("length", NULL, NULL,
                         0, G_MAXUINT64, 0,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_OFFSET] =
    g_param_spec_uint64 ("offset", NULL, NULL,
                         0, G_MAXUINT64, 0,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_PATH] =
    g_param_spec_string ("path", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_mapped_file_init (SysprofMappedFile *self)
{
}

/**
 * sysprof_mapped_file_new:
 * @path: a ref-string for use with g_ref_string_acquire().
 * @address: the address where the file is mapped
 * @offset: the offset of the file which is mapped at @address
 * @length: the length of the memory mapping in bytes
 * @inode: an optional inode of the mapped file, used for validation checks
 *
 * Creates a new #SysprofMappedFile which represents a mapping within the
 * address range of a process.
 *
 * Returns: (transfer full): a #SysprofMappedFile
 */
SysprofMappedFile *
sysprof_mapped_file_new (char    *path,
                         guint64  address,
                         guint64  offset,
                         guint64  length,
                         gint64   inode)
{
  SysprofMappedFile *self;

  g_return_val_if_fail (path != NULL, NULL);

  self = g_object_new (SYSPROF_TYPE_MAPPED_FILE, NULL);
  self->path = g_ref_string_acquire (path);
  self->address = address;
  self->offset = offset;
  self->length = length;
  self->inode = inode;

  return self;
}

guint64
sysprof_mapped_file_get_address (SysprofMappedFile *self)
{
  return self->address;
}

guint64
sysprof_mapped_file_get_offset (SysprofMappedFile *self)
{
  return self->offset;
}

guint64
sysprof_mapped_file_get_length (SysprofMappedFile *self)
{
  return self->length;
}

gint64
sysprof_mapped_file_get_inode (SysprofMappedFile *self)
{
  return self->inode;
}

const char *
sysprof_mapped_file_get_path (SysprofMappedFile *self)
{
  return self->path;
}
