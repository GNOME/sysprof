/* sysprof-document-overlay.c
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
#include "sysprof-document-overlay.h"

struct _SysprofDocumentOverlay
{
  SysprofDocumentFrame parent_instance;
};

struct _SysprofDocumentOverlayClass
{
  SysprofDocumentFrameClass parent_class;
};

enum {
  PROP_0,
  PROP_DESTINATION,
  PROP_LAYER,
  PROP_SOURCE,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofDocumentOverlay, sysprof_document_overlay, SYSPROF_TYPE_DOCUMENT_FRAME)

static GParamSpec *properties [N_PROPS];

static void
sysprof_document_overlay_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  SysprofDocumentOverlay *self = SYSPROF_DOCUMENT_OVERLAY (object);

  switch (prop_id)
    {
    case PROP_DESTINATION:
      g_value_set_string (value, sysprof_document_overlay_get_destination (self));
      break;

    case PROP_LAYER:
      g_value_set_uint (value, sysprof_document_overlay_get_layer (self));
      break;

    case PROP_SOURCE:
      g_value_set_string (value, sysprof_document_overlay_get_source (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_document_overlay_class_init (SysprofDocumentOverlayClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = sysprof_document_overlay_get_property;

  properties [PROP_LAYER] =
    g_param_spec_uint ("layer", NULL, NULL,
                       0, G_MAXUINT, 0,
                       (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_DESTINATION] =
    g_param_spec_string ("destination", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SOURCE] =
    g_param_spec_string ("source", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_document_overlay_init (SysprofDocumentOverlay *self)
{
}

guint
sysprof_document_overlay_get_layer (SysprofDocumentOverlay *self)
{
  const SysprofCaptureOverlay *overlay;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_OVERLAY (self), 0);

  overlay = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureOverlay);

  return overlay->layer;
}

const char *
sysprof_document_overlay_get_source (SysprofDocumentOverlay *self)
{
  const SysprofCaptureOverlay *overlay;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_OVERLAY (self), 0);

  overlay = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureOverlay);

  return SYSPROF_DOCUMENT_FRAME_CSTRING (self, overlay->data);
}

const char *
sysprof_document_overlay_get_destination (SysprofDocumentOverlay *self)
{
  const SysprofCaptureOverlay *overlay;
  guint16 offset;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_OVERLAY (self), 0);

  overlay = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureOverlay);

  offset = SYSPROF_DOCUMENT_FRAME_UINT16 (self, overlay->src_len);

  return SYSPROF_DOCUMENT_FRAME_CSTRING (self, &overlay->data[offset+1]);
}
