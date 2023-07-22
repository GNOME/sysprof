/*
 * sysprof-document-mmap.c
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

#include "sysprof-document-frame-private.h"
#include "sysprof-document-mmap.h"

struct _SysprofDocumentMmap
{
  SysprofDocumentFrame parent_instance;
};

struct _SysprofDocumentMmapClass
{
  SysprofDocumentFrameClass parent_class;
};

enum {
  PROP_0,
  PROP_BUILD_ID,
  PROP_END_ADDRESS,
  PROP_FILE,
  PROP_FILE_INODE,
  PROP_FILE_OFFSET,
  PROP_START_ADDRESS,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofDocumentMmap, sysprof_document_mmap, SYSPROF_TYPE_DOCUMENT_FRAME)

static GParamSpec *properties [N_PROPS];

static void
sysprof_document_mmap_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  SysprofDocumentMmap *self = SYSPROF_DOCUMENT_MMAP (object);

  switch (prop_id)
    {
    case PROP_START_ADDRESS:
      g_value_set_uint64 (value, sysprof_document_mmap_get_start_address (self));
      break;

    case PROP_END_ADDRESS:
      g_value_set_uint64 (value, sysprof_document_mmap_get_end_address (self));
      break;

    case PROP_FILE:
      g_value_set_string (value, sysprof_document_mmap_get_file (self));
      break;

    case PROP_FILE_OFFSET:
      g_value_set_uint64 (value, sysprof_document_mmap_get_file_offset (self));
      break;

    case PROP_FILE_INODE:
      g_value_set_uint64 (value, sysprof_document_mmap_get_file_inode (self));
      break;

    case PROP_BUILD_ID:
      g_value_set_string (value, sysprof_document_mmap_get_build_id (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_document_mmap_class_init (SysprofDocumentMmapClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = sysprof_document_mmap_get_property;

  properties [PROP_START_ADDRESS] =
    g_param_spec_uint64 ("start-address", NULL, NULL,
                         0, G_MAXUINT64, 0,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_END_ADDRESS] =
    g_param_spec_uint64 ("end-address", NULL, NULL,
                         0, G_MAXUINT64, 0,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_FILE] =
    g_param_spec_string ("file", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_FILE_INODE] =
    g_param_spec_uint64 ("file-inode", NULL, NULL,
                         0, G_MAXUINT64, 0,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_FILE_OFFSET] =
    g_param_spec_uint64 ("file-offset", NULL, NULL,
                         0, G_MAXUINT64, 0,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_BUILD_ID] =
    g_param_spec_string ("build-id", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_document_mmap_init (SysprofDocumentMmap *self)
{
}

guint64
sysprof_document_mmap_get_start_address (SysprofDocumentMmap *self)
{
  const SysprofCaptureMap *mmap;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_MMAP (self), 0);

  mmap = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureMap);

  return SYSPROF_DOCUMENT_FRAME_UINT64 (self, mmap->start);
}

guint64
sysprof_document_mmap_get_end_address (SysprofDocumentMmap *self)
{
  const SysprofCaptureMap *mmap;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_MMAP (self), 0);

  mmap = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureMap);

  return SYSPROF_DOCUMENT_FRAME_UINT64 (self, mmap->end);
}

guint64
sysprof_document_mmap_get_file_inode (SysprofDocumentMmap *self)
{
  const SysprofCaptureMap *mmap;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_MMAP (self), 0);

  mmap = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureMap);

  return SYSPROF_DOCUMENT_FRAME_UINT64 (self, mmap->inode);
}

guint64
sysprof_document_mmap_get_file_offset (SysprofDocumentMmap *self)
{
  const SysprofCaptureMap *mmap;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_MMAP (self), 0);

  mmap = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureMap);

  return SYSPROF_DOCUMENT_FRAME_UINT64 (self, mmap->offset);
}

const char *
sysprof_document_mmap_get_file (SysprofDocumentMmap *self)
{
  const SysprofCaptureMap *mmap;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_MMAP (self), NULL);

  mmap = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureMap);

  return SYSPROF_DOCUMENT_FRAME_CSTRING (self, mmap->filename);
}

const char *
sysprof_document_mmap_get_build_id (SysprofDocumentMmap *self)
{
  const char *file;
  const char *build_id;
  gsize len;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_MMAP (self), NULL);

  if (!(file = sysprof_document_mmap_get_file (self)))
    return NULL;

  /* The build-id may be tacked on after the filename if after the
   * Nil byte we get '@'. SYSPROF_DOCUMENT_FRAME_CSTRING() will check
   * for bounds so we can feed it a position we don't know is part
   * of our frame or not. We expect "FILE\0@BUILD_ID_IN_HEX\0".
   */

  len = strlen (file);

  if (!(build_id = SYSPROF_DOCUMENT_FRAME_CSTRING (self, &file[len+1])))
    return NULL;

  if (build_id[0] != '@')
    return NULL;

  return &build_id[1];
}
