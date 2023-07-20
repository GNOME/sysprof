/* sysprof-document-metadata.c
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
#include "sysprof-document-metadata.h"

struct _SysprofDocumentMetadata
{
  SysprofDocumentFrame parent_instance;
};

struct _SysprofDocumentMetadataClass
{
  SysprofDocumentFrameClass parent_class;
};

enum {
  PROP_0,
  PROP_ID,
  PROP_VALUE,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofDocumentMetadata, sysprof_document_metadata, SYSPROF_TYPE_DOCUMENT_FRAME)

static GParamSpec *properties [N_PROPS];

static void
sysprof_document_metadata_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  SysprofDocumentMetadata *self = SYSPROF_DOCUMENT_METADATA (object);

  switch (prop_id)
    {
    case PROP_ID:
      g_value_set_string (value, sysprof_document_metadata_get_id (self));
      break;

    case PROP_VALUE:
      g_value_set_string (value, sysprof_document_metadata_get_value (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_document_metadata_class_init (SysprofDocumentMetadataClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = sysprof_document_metadata_get_property;

  properties [PROP_ID] =
    g_param_spec_string ("id", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_VALUE] =
    g_param_spec_string ("value", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_document_metadata_init (SysprofDocumentMetadata *self)
{
}

const char *
sysprof_document_metadata_get_id (SysprofDocumentMetadata *self)
{
  const SysprofCaptureMetadata *meta;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_METADATA (self), 0);

  meta = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureMetadata);

  return SYSPROF_DOCUMENT_FRAME_CSTRING (self, meta->id);
}

const char *
sysprof_document_metadata_get_value (SysprofDocumentMetadata *self)
{
  const SysprofCaptureMetadata *meta;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_METADATA (self), 0);

  meta = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureMetadata);

  return SYSPROF_DOCUMENT_FRAME_CSTRING (self, meta->metadata);
}
