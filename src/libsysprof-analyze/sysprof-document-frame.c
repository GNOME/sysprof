/* sysprof-document-frame.c
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
#include "sysprof-document-sample.h"

G_DEFINE_TYPE (SysprofDocumentFrame, sysprof_document_frame, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_CPU,
  PROP_PID,
  PROP_TIME,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
sysprof_document_frame_finalize (GObject *object)
{
  SysprofDocumentFrame *self = (SysprofDocumentFrame *)object;

  g_clear_pointer (&self->mapped_file, g_mapped_file_unref);

  self->frame = NULL;
  self->needs_swap = 0;

  G_OBJECT_CLASS (sysprof_document_frame_parent_class)->finalize (object);
}

static void
sysprof_document_frame_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  SysprofDocumentFrame *self = SYSPROF_DOCUMENT_FRAME (object);

  switch (prop_id)
    {
    case PROP_CPU:
      g_value_set_int (value, sysprof_document_frame_get_cpu (self));
      break;

    case PROP_PID:
      g_value_set_int (value, sysprof_document_frame_get_pid (self));
      break;

    case PROP_TIME:
      g_value_set_int64 (value, sysprof_document_frame_get_time (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_document_frame_class_init (SysprofDocumentFrameClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_document_frame_finalize;
  object_class->get_property = sysprof_document_frame_get_property;

  properties[PROP_CPU] =
    g_param_spec_int ("cpu", NULL, NULL,
                      G_MININT32,
                      G_MAXINT32,
                      -1,
                      (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_PID] =
    g_param_spec_int ("pid", NULL, NULL,
                      G_MININT32,
                      G_MAXINT32,
                      -1,
                      (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_TIME] =
    g_param_spec_int64 ("time", NULL, NULL,
                        G_MININT64,
                        G_MAXINT64,
                        0,
                        (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_document_frame_init (SysprofDocumentFrame *self)
{
}

SysprofDocumentFrame *
_sysprof_document_frame_new (GMappedFile               *mapped_file,
                             const SysprofCaptureFrame *frame,
                             guint16                    frame_len,
                             gboolean                   needs_swap)
{
  GType gtype = SYSPROF_TYPE_DOCUMENT_FRAME;
  SysprofDocumentFrame *self;

  if (frame->type == SYSPROF_CAPTURE_FRAME_SAMPLE)
    gtype = SYSPROF_TYPE_DOCUMENT_SAMPLE;

  self = g_object_new (gtype, NULL);
  self->mapped_file = g_mapped_file_ref (mapped_file);
  self->frame = frame;
  self->frame_len = frame_len;
  self->needs_swap = !!needs_swap;

  return self;
}

int
sysprof_document_frame_get_cpu (SysprofDocumentFrame *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_FRAME (self), 0);

  return SYSPROF_DOCUMENT_FRAME_INT32 (self, self->frame->cpu);
}

int
sysprof_document_frame_get_pid (SysprofDocumentFrame *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_FRAME (self), 0);

  return SYSPROF_DOCUMENT_FRAME_INT32 (self, self->frame->pid);
}

gint64
sysprof_document_frame_get_time (SysprofDocumentFrame *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_FRAME (self), 0);

  return SYSPROF_DOCUMENT_FRAME_INT64 (self, self->frame->time);
}
