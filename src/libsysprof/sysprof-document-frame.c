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

#include <glib/gi18n.h>

#include "sysprof-document-frame-private.h"

#include "sysprof-document-allocation.h"
#include "sysprof-document-ctrdef.h"
#include "sysprof-document-ctrset.h"
#include "sysprof-document-dbus-message.h"
#include "sysprof-document-exit.h"
#include "sysprof-document-file-chunk.h"
#include "sysprof-document-fork.h"
#include "sysprof-document-log.h"
#include "sysprof-document-jitmap.h"
#include "sysprof-document-mark.h"
#include "sysprof-document-metadata.h"
#include "sysprof-document-mmap.h"
#include "sysprof-document-overlay.h"
#include "sysprof-document-process.h"
#include "sysprof-document-sample.h"

G_DEFINE_TYPE (SysprofDocumentFrame, sysprof_document_frame, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_CPU,
  PROP_PID,
  PROP_TIME,
  PROP_TIME_OFFSET,
  PROP_TIME_STRING,
  PROP_TOOLTIP,
  PROP_TYPE_NAME,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static char *
sysprof_document_frame_real_dup_tooltip (SysprofDocumentFrame *self)
{
  g_autofree char *time_string = sysprof_document_frame_dup_time_string (self);

  return g_strdup_printf ("%s: %s",
                          time_string,
                          SYSPROF_DOCUMENT_FRAME_GET_CLASS (self)->type_name);
}

static const char *
sysprof_document_frame_get_type_name (SysprofDocumentFrame *self)
{
  return g_dgettext (GETTEXT_PACKAGE, SYSPROF_DOCUMENT_FRAME_GET_CLASS (self)->type_name);
}

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

    case PROP_TIME_OFFSET:
      g_value_set_int64 (value, sysprof_document_frame_get_time_offset (self));
      break;

    case PROP_TIME_STRING:
      g_value_take_string (value, sysprof_document_frame_dup_time_string (self));
      break;

    case PROP_TOOLTIP:
      g_value_take_string (value, sysprof_document_frame_dup_tooltip (self));
      break;

    case PROP_TYPE_NAME:
      g_value_set_static_string (value, sysprof_document_frame_get_type_name (self));
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

  klass->type_name = N_("Frame");
  klass->dup_tooltip = sysprof_document_frame_real_dup_tooltip;

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

  properties[PROP_TIME_OFFSET] =
    g_param_spec_int64 ("time-offset", NULL, NULL,
                        G_MININT64,
                        G_MAXINT64,
                        0,
                        (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_TIME_STRING] =
    g_param_spec_string ("time-string", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_TYPE_NAME] =
    g_param_spec_string ("type-name", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_TOOLTIP] =
    g_param_spec_string ("tooltip", NULL, NULL,
                         NULL,
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
                             gboolean                   needs_swap,
                             gint64                     begin_time,
                             gint64                     end_time)
{
  SysprofDocumentFrame *self;
  GType gtype;

  switch (frame->type)
    {
    case SYSPROF_CAPTURE_FRAME_SAMPLE:
      gtype = SYSPROF_TYPE_DOCUMENT_SAMPLE;
      break;

    case SYSPROF_CAPTURE_FRAME_MAP:
      gtype = SYSPROF_TYPE_DOCUMENT_MMAP;
      break;

    case SYSPROF_CAPTURE_FRAME_LOG:
      gtype = SYSPROF_TYPE_DOCUMENT_LOG;
      break;

    case SYSPROF_CAPTURE_FRAME_MARK:
      gtype = SYSPROF_TYPE_DOCUMENT_MARK;
      break;

    case SYSPROF_CAPTURE_FRAME_METADATA:
      gtype = SYSPROF_TYPE_DOCUMENT_METADATA;
      break;

    case SYSPROF_CAPTURE_FRAME_PROCESS:
      gtype = SYSPROF_TYPE_DOCUMENT_PROCESS;
      break;

    case SYSPROF_CAPTURE_FRAME_EXIT:
      gtype = SYSPROF_TYPE_DOCUMENT_EXIT;
      break;

    case SYSPROF_CAPTURE_FRAME_FORK:
      gtype = SYSPROF_TYPE_DOCUMENT_FORK;
      break;

    case SYSPROF_CAPTURE_FRAME_ALLOCATION:
      gtype = SYSPROF_TYPE_DOCUMENT_ALLOCATION;
      break;

    case SYSPROF_CAPTURE_FRAME_FILE_CHUNK:
      gtype = SYSPROF_TYPE_DOCUMENT_FILE_CHUNK;
      break;

    case SYSPROF_CAPTURE_FRAME_OVERLAY:
      gtype = SYSPROF_TYPE_DOCUMENT_OVERLAY;
      break;

    case SYSPROF_CAPTURE_FRAME_JITMAP:
      gtype = SYSPROF_TYPE_DOCUMENT_JITMAP;
      break;

    case SYSPROF_CAPTURE_FRAME_CTRDEF:
      gtype = SYSPROF_TYPE_DOCUMENT_CTRDEF;
      break;

    case SYSPROF_CAPTURE_FRAME_CTRSET:
      gtype = SYSPROF_TYPE_DOCUMENT_CTRSET;
      break;

    case SYSPROF_CAPTURE_FRAME_DBUS_MESSAGE:
      gtype = SYSPROF_TYPE_DOCUMENT_DBUS_MESSAGE;
      break;

    default:
      gtype = SYSPROF_TYPE_DOCUMENT_FRAME;
      break;
    }


  self = g_object_new (gtype, NULL);
  self->mapped_file = g_mapped_file_ref (mapped_file);
  self->frame = frame;
  self->frame_len = frame_len;
  self->needs_swap = !!needs_swap;

  self->time_offset = CLAMP (sysprof_document_frame_get_time (self) - begin_time, 0, G_MAXINT64);

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

gint64
sysprof_document_frame_get_time_offset (SysprofDocumentFrame *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_FRAME (self), 0);

  return self->time_offset;
}

gboolean
sysprof_document_frame_equal (const SysprofDocumentFrame *a,
                              const SysprofDocumentFrame *b)
{
  return a->frame == b->frame;
}

/**
 * sysprof_document_frame_dup_time_string:
 * @self: a #SysprofDocumentFrame
 *
 * Gets the time formatted as a string such as `00:00:00.1234`.
 *
 * Returns: (transfer full): a new string
 */
char *
sysprof_document_frame_dup_time_string (SysprofDocumentFrame *self)
{
  int hours;
  int minutes;
  int seconds;
  double time;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_FRAME (self), NULL);

  time = self->time_offset / (double)SYSPROF_NSEC_PER_SEC;

  hours = time / (60 * 60);
  time -= hours * (60 * 60);

  minutes = time / 60;
  time -= minutes * 60;

  seconds = time / SYSPROF_NSEC_PER_SEC;
  time -= seconds * SYSPROF_NSEC_PER_SEC;

  return g_strdup_printf ("%02d:%02d:%02d.%04d", hours, minutes, seconds, (int)(time * 10000));
}

/**
 * sysprof_document_frame_dup_tooltip:
 * @self: a #SysprofDocumentFrame
 *
 * Returns a new string containing suggested tooltip text.
 *
 * Returns: (transfer full): a string
 */
char *
sysprof_document_frame_dup_tooltip (SysprofDocumentFrame *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_FRAME (self), NULL);

  return SYSPROF_DOCUMENT_FRAME_GET_CLASS (self)->dup_tooltip (self);
}
