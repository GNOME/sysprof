/* sysprof-document-file-chunk.c
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

#include "sysprof-document-file-chunk.h"

struct _SysprofDocumentFileChunk
{
  SysprofDocumentFrame parent_instance;
};

struct _SysprofDocumentFileChunkClass
{
  SysprofDocumentFrameClass parent_instance;
};

enum {
  PROP_0,
  PROP_IS_LAST,
  PROP_SIZE,
  PROP_PATH,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofDocumentFileChunk, sysprof_document_file_chunk, SYSPROF_TYPE_DOCUMENT_FRAME)

static GParamSpec *properties [N_PROPS];

static void
sysprof_document_file_chunk_get_property (GObject    *object,
                                          guint       prop_id,
                                          GValue     *value,
                                          GParamSpec *pspec)
{
  SysprofDocumentFileChunk *self = SYSPROF_DOCUMENT_FILE_CHUNK (object);

  switch (prop_id)
    {
    case PROP_IS_LAST:
      g_value_set_boolean (value, sysprof_document_file_chunk_get_is_last (self));
      break;

    case PROP_SIZE:
      g_value_set_uint (value, sysprof_document_file_chunk_get_size (self));
      break;

    case PROP_PATH:
      g_value_set_string (value, sysprof_document_file_chunk_get_path (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_document_file_chunk_class_init (SysprofDocumentFileChunkClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = sysprof_document_file_chunk_get_property;

  properties [PROP_IS_LAST] =
    g_param_spec_boolean ("is-last", NULL, NULL,
                          FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SIZE] =
    g_param_spec_uint ("size", NULL, NULL,
                       0, G_MAXUINT16, 0,
                       (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_PATH] =
    g_param_spec_string ("path", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_document_file_chunk_init (SysprofDocumentFileChunk *self)
{
}

gboolean
sysprof_document_file_chunk_get_is_last (SysprofDocumentFileChunk *self)
{
  const SysprofCaptureFileChunk *file_chunk;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_FILE_CHUNK (self), FALSE);

  file_chunk = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureFileChunk);

  return file_chunk->is_last;
}

guint
sysprof_document_file_chunk_get_size (SysprofDocumentFileChunk *self)
{
  const SysprofCaptureFileChunk *file_chunk;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_FILE_CHUNK (self), FALSE);

  file_chunk = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureFileChunk);

  return SYSPROF_DOCUMENT_FRAME_UINT16 (self, file_chunk->len);
}

const char *
sysprof_document_file_chunk_get_path (SysprofDocumentFileChunk *self)
{
  const SysprofCaptureFileChunk *file_chunk;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_FILE_CHUNK (self), FALSE);

  file_chunk = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureFileChunk);

  return SYSPROF_DOCUMENT_FRAME_CSTRING (self, file_chunk->path);
}

const guint8 *
sysprof_document_file_chunk_get_data (SysprofDocumentFileChunk *self,
                                      guint                    *size)
{
  const SysprofCaptureFileChunk *file_chunk;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_FILE_CHUNK (self), FALSE);

  file_chunk = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureFileChunk);

  if (size != NULL)
    *size = sysprof_document_file_chunk_get_size (self);

  return file_chunk->data;
}
