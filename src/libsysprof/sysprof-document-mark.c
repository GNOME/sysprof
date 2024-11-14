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

#include <glib/gi18n.h>

#include "sysprof-document-frame-private.h"
#include "sysprof-document-mark.h"
#include "sysprof-time-span.h"

struct _SysprofDocumentMark
{
  SysprofDocumentFrame parent_instance;
};

struct _SysprofDocumentMarkClass
{
  SysprofDocumentFrameClass parent_class;
};


enum {
  PROP_0,
  PROP_DURATION,
  PROP_END_TIME,
  PROP_GROUP,
  PROP_MESSAGE,
  PROP_NAME,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofDocumentMark, sysprof_document_mark, SYSPROF_TYPE_DOCUMENT_FRAME)

static GParamSpec *properties [N_PROPS];

static char *
get_time_str (gint64 o)
{
  char str[32];

  if (o == 0)
    g_snprintf (str, sizeof str, "%.3lf s", .0);
  else if (o < 1000000)
    g_snprintf (str, sizeof str, "%.3lf μs", o/1000.);
  else if (o < SYSPROF_NSEC_PER_SEC)
    g_snprintf (str, sizeof str, "%.3lf ms", o/1000000.);
  else
    g_snprintf (str, sizeof str, "%.3lf s", o/(double)SYSPROF_NSEC_PER_SEC);

  return g_strdup (str);
}

static char *
sysprof_document_mark_dup_tooltip (SysprofDocumentFrame *frame)
{
  SysprofDocumentMark *self = (SysprofDocumentMark *)frame;
  g_autofree char *duration_string = NULL;
  g_autofree char *time_string = NULL;

  g_assert (SYSPROF_IS_DOCUMENT_MARK (self));

  time_string = sysprof_document_frame_dup_time_string (SYSPROF_DOCUMENT_FRAME (self));
  duration_string = get_time_str (sysprof_document_mark_get_duration (self));

  return g_strdup_printf ("%s (%s): %s / %s: %s",
                          time_string,
                          duration_string,
                          sysprof_document_mark_get_group (self),
                          sysprof_document_mark_get_name (self),
                          sysprof_document_mark_get_message (self));
}

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

    case PROP_END_TIME:
      g_value_set_int64 (value, sysprof_document_mark_get_end_time (self));
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
  SysprofDocumentFrameClass *document_frame_class = SYSPROF_DOCUMENT_FRAME_CLASS (klass);

  object_class->get_property = sysprof_document_mark_get_property;

  document_frame_class->type_name = N_("Mark");
  document_frame_class->dup_tooltip = sysprof_document_mark_dup_tooltip;

  properties [PROP_DURATION] =
    g_param_spec_int64 ("duration", NULL, NULL,
                        G_MININT64, G_MAXINT64, 0,
                        (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_END_TIME] =
    g_param_spec_int64 ("end-time", NULL, NULL,
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

gint64
sysprof_document_mark_get_end_time (SysprofDocumentMark *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_MARK (self), 0);

  return sysprof_document_frame_get_time (SYSPROF_DOCUMENT_FRAME (self))
       + sysprof_document_mark_get_duration (self);
}
