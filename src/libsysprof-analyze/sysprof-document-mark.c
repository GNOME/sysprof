/* sysprof-document-mark.c
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
#include "sysprof-document-mark.h"

enum {
  PROP_0,
  PROP_DURATION,
  PROP_GROUP,
  PROP_MESSAGE,
  PROP_NAME,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofDocumentMark, sysprof_document_mark, SYSPROF_TYPE_DOCUMENT_FRAME)

static GParamSpec *properties [N_PROPS];

static void
sysprof_document_mark_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  SysprofDocumentMark *self = SYSPROF_DOCUMENT_MARK (object);

  switch (prop_id)
    {
    case PROP_DURATION:
      g_value_set_int64 (value, sysprof_document_mark_get_duration (self));
      break;

    case PROP_NAME:
      g_value_set_string (value, sysprof_document_mark_get_name (self));
      break;

    case PROP_GROUP:
      g_value_set_string (value, sysprof_document_mark_get_group (self));
      break;

    case PROP_MESSAGE:
      g_value_set_string (value, sysprof_document_mark_get_message (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_document_mark_class_init (SysprofDocumentMarkClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = sysprof_document_mark_get_property;

  properties [PROP_DURATION] =
    g_param_spec_int64 ("duration", NULL, NULL,
                        G_MININT64, G_MAXINT64, 0,
                        (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_GROUP] =
    g_param_spec_string ("group", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_NAME] =
    g_param_spec_string ("name", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_MESSAGE] =
    g_param_spec_string ("message", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_document_mark_init (SysprofDocumentMark *self)
{
}

gint64
sysprof_document_mark_get_duration (SysprofDocumentMark *self)
{
  const SysprofCaptureMark *mark;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_MARK (self), 0);

  mark = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureMark);

  return SYSPROF_DOCUMENT_FRAME_INT64 (self, mark->duration);
}

const char *
sysprof_document_mark_get_group (SysprofDocumentMark *self)
{
  const SysprofCaptureMark *mark;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_MARK (self), 0);

  mark = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureMark);

  return SYSPROF_DOCUMENT_FRAME_CSTRING (self, mark->group);
}

const char *
sysprof_document_mark_get_name (SysprofDocumentMark *self)
{
  const SysprofCaptureMark *mark;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_MARK (self), 0);

  mark = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureMark);

  return SYSPROF_DOCUMENT_FRAME_CSTRING (self, mark->name);
}

const char *
sysprof_document_mark_get_message (SysprofDocumentMark *self)
{
  const SysprofCaptureMark *mark;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_MARK (self), 0);

  mark = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureMark);

  return SYSPROF_DOCUMENT_FRAME_CSTRING (self, mark->message);
}

/**
 * sysprof_document_mark_get_time_fraction:
 * @self: a #SysprofDocumentMark
 * @begin_fraction: (out) (nullable): a location for the begin
 *   time as a fraction
 * @end_fraction: (out) (nullable): a location for the end
 *   time as a fraction
 *
 * Gets the begin/end time of the mark as a fraction between 0 and 1.
 *
 * 0 is the beginning of the capture, 1 is the end of the capture.
 */
void
sysprof_document_mark_get_time_fraction (SysprofDocumentMark *self,
                                         double              *begin_fraction,
                                         double              *end_fraction)
{
  g_return_if_fail (SYSPROF_IS_DOCUMENT_MARK (self));

  if (begin_fraction)
    *begin_fraction = self->begin_fraction;

  if (end_fraction)
    *end_fraction = self->end_fraction;
}
