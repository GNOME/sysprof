/* sysprof-session.c
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

#include "sysprof-session.h"

struct _SysprofSession
{
  GObject          parent_instance;

  SysprofDocument *document;
  GtkEveryFilter  *filter;

  SysprofTimeSpan  selected_time;
  SysprofTimeSpan  visible_time;
};

enum {
  PROP_0,
  PROP_DOCUMENT,
  PROP_FILTER,
  PROP_SELECTED_TIME,
  PROP_VISIBLE_TIME,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofSession, sysprof_session, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static void
sysprof_session_set_document (SysprofSession  *self,
                              SysprofDocument *document)
{
  g_assert (SYSPROF_IS_SESSION (self));
  g_assert (SYSPROF_IS_DOCUMENT (document));

  g_set_object (&self->document, document);

  self->visible_time = *sysprof_document_get_time_span (document);
  self->selected_time = self->visible_time;
}

static void
sysprof_session_dispose (GObject *object)
{
  SysprofSession *self = (SysprofSession *)object;

  g_clear_object (&self->document);
  g_clear_object (&self->filter);

  G_OBJECT_CLASS (sysprof_session_parent_class)->dispose (object);
}

static void
sysprof_session_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  SysprofSession *self = SYSPROF_SESSION (object);

  switch (prop_id)
    {
    case PROP_DOCUMENT:
      g_value_set_object (value, sysprof_session_get_document (self));
      break;

    case PROP_FILTER:
      g_value_set_object (value, sysprof_session_get_filter (self));
      break;

    case PROP_SELECTED_TIME:
      g_value_set_boxed (value, sysprof_session_get_selected_time (self));
      break;

    case PROP_VISIBLE_TIME:
      g_value_set_boxed (value, sysprof_session_get_visible_time (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_session_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  SysprofSession *self = SYSPROF_SESSION (object);

  switch (prop_id)
    {
    case PROP_DOCUMENT:
      sysprof_session_set_document (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_session_class_init (SysprofSessionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = sysprof_session_dispose;
  object_class->get_property = sysprof_session_get_property;
  object_class->set_property = sysprof_session_set_property;

  properties [PROP_DOCUMENT] =
    g_param_spec_object ("document", NULL, NULL,
                         SYSPROF_TYPE_DOCUMENT,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_FILTER] =
    g_param_spec_object ("filter", NULL, NULL,
                         GTK_TYPE_FILTER,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SELECTED_TIME] =
    g_param_spec_boxed ("selected-time", NULL, NULL,
                        SYSPROF_TYPE_TIME_SPAN,
                        (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_VISIBLE_TIME] =
    g_param_spec_boxed ("visible-time", NULL, NULL,
                        SYSPROF_TYPE_TIME_SPAN,
                        (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_session_init (SysprofSession *self)
{
  self->filter = gtk_every_filter_new ();
}

SysprofSession *
sysprof_session_new (SysprofDocument *document)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (document), NULL);

  return g_object_new (SYSPROF_TYPE_SESSION,
                       "document", document,
                       NULL);
}

/**
 * sysprof_session_get_document:
 * @self: a #SysprofSession
 *
 * Returns: (transfer none): a #SysprofDocument
 */
SysprofDocument *
sysprof_session_get_document (SysprofSession *self)
{
  g_return_val_if_fail (SYSPROF_IS_SESSION (self), NULL);

  return self->document;
}

/**
 * sysprof_session_get_filter:
 * @self: a #SysprofSession
 *
 * Gets the filter for the session which can remove #SysprofDocumentFrame
 * which are not matching the current selection filters.
 *
 * Returns: (transfer none) (nullable): a #GtkFilter
 */
GtkFilter *
sysprof_session_get_filter (SysprofSession *self)
{
  g_return_val_if_fail (SYSPROF_IS_SESSION (self), NULL);

  return GTK_FILTER (self->filter);
}

const SysprofTimeSpan *
sysprof_session_get_selected_time (SysprofSession *self)
{
  g_return_val_if_fail (SYSPROF_IS_SESSION (self), NULL);

  return &self->selected_time;
}

const SysprofTimeSpan *
sysprof_session_get_visible_time (SysprofSession *self)
{
  g_return_val_if_fail (SYSPROF_IS_SESSION (self), NULL);

  return &self->visible_time;
}

void
sysprof_session_select_time (SysprofSession        *self,
                             const SysprofTimeSpan *time_span)
{
  SysprofTimeSpan document_time_span;
  gboolean emit_for_visible = FALSE;

  g_return_if_fail (SYSPROF_IS_SESSION (self));

  document_time_span = *sysprof_document_get_time_span (self->document);

  if (time_span == NULL)
    time_span = &document_time_span;

  self->selected_time = *time_span;

  if (self->visible_time.begin_nsec > time_span->begin_nsec)
    {
      self->visible_time.begin_nsec = time_span->begin_nsec;
      emit_for_visible = TRUE;
    }

  if (self->visible_time.end_nsec < time_span->end_nsec)
    {
      self->visible_time.end_nsec = time_span->end_nsec;
      emit_for_visible = TRUE;
    }

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SELECTED_TIME]);

  if (emit_for_visible)
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VISIBLE_TIME]);
}
