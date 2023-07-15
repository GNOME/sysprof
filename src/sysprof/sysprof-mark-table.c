/* sysprof-mark-table.c
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

#include "sysprof-css-private.h"
#include "sysprof-time-filter-model.h"
#include "sysprof-mark-table.h"
#include "sysprof-time-label.h"

#include "sysprof-resources.h"

struct _SysprofMarkTable
{
  GtkWidget            parent_instance;

  SysprofSession      *session;

  GtkWidget           *box;
  GtkColumnView       *column_view;
  GtkColumnViewColumn *start_column;
};

enum {
  PROP_0,
  PROP_SESSION,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofMarkTable, sysprof_mark_table, GTK_TYPE_WIDGET)

static GParamSpec *properties [N_PROPS];

static void
sysprof_mark_table_activate_cb (SysprofMarkTable *self,
                                guint             position,
                                GtkColumnView    *column_view)
{
  g_autoptr(SysprofDocumentMark) mark = NULL;
  SysprofTimeSpan time_span;
  GListModel *model;

  g_assert (SYSPROF_IS_MARK_TABLE (self));
  g_assert (GTK_IS_COLUMN_VIEW (column_view));

  if (self->session == NULL)
    return;

  model = G_LIST_MODEL (gtk_column_view_get_model (column_view));
  mark = g_list_model_get_item (model, position);

  time_span.begin_nsec = sysprof_document_frame_get_time (SYSPROF_DOCUMENT_FRAME (mark));
  time_span.end_nsec = time_span.begin_nsec + sysprof_document_mark_get_duration (mark);

  if (time_span.end_nsec != time_span.begin_nsec)
    sysprof_session_select_time (self->session, &time_span);
}

static void
sysprof_mark_table_dispose (GObject *object)
{
  SysprofMarkTable *self = (SysprofMarkTable *)object;

  if (self->session)
    g_clear_object (&self->session);

  g_clear_pointer (&self->box, gtk_widget_unparent);

  G_OBJECT_CLASS (sysprof_mark_table_parent_class)->dispose (object);
}

static void
sysprof_mark_table_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  SysprofMarkTable *self = SYSPROF_MARK_TABLE (object);

  switch (prop_id)
    {
    case PROP_SESSION:
      g_value_set_object (value, sysprof_mark_table_get_session (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_mark_table_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  SysprofMarkTable *self = SYSPROF_MARK_TABLE (object);

  switch (prop_id)
    {
    case PROP_SESSION:
      sysprof_mark_table_set_session (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_mark_table_class_init (SysprofMarkTableClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_mark_table_dispose;
  object_class->get_property = sysprof_mark_table_get_property;
  object_class->set_property = sysprof_mark_table_set_property;

  properties [PROP_SESSION] =
    g_param_spec_object ("session", NULL, NULL,
                         SYSPROF_TYPE_SESSION,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-mark-table.ui");
  gtk_widget_class_set_css_name (widget_class, "marktable");
  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_bind_template_child (widget_class, SysprofMarkTable, box);
  gtk_widget_class_bind_template_child (widget_class, SysprofMarkTable, column_view);
  gtk_widget_class_bind_template_child (widget_class, SysprofMarkTable, start_column);
  gtk_widget_class_bind_template_callback (widget_class, sysprof_mark_table_activate_cb);

  g_resources_register (sysprof_get_resource ());

  g_type_ensure (SYSPROF_TYPE_DOCUMENT_MARK);
  g_type_ensure (SYSPROF_TYPE_TIME_FILTER_MODEL);
  g_type_ensure (SYSPROF_TYPE_TIME_LABEL);
}

static void
sysprof_mark_table_init (SysprofMarkTable *self)
{
  _sysprof_css_init ();

  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_column_view_sort_by_column (self->column_view,
                                  self->start_column,
                                  GTK_SORT_ASCENDING);
}

GtkWidget *
sysprof_mark_table_new (void)
{
  return g_object_new (SYSPROF_TYPE_MARK_TABLE, NULL);
}

/**
 * sysprof_mark_table_get_session:
 * @self: a #SysprofMarkTable
 *
 * Returns: (transfer none) (nullable): a #SysprofSession or %NULL
 */
SysprofSession *
sysprof_mark_table_get_session (SysprofMarkTable *self)
{
  g_return_val_if_fail (SYSPROF_IS_MARK_TABLE (self), NULL);

  return self->session;
}

void
sysprof_mark_table_set_session (SysprofMarkTable *self,
                                SysprofSession   *session)
{
  g_return_if_fail (SYSPROF_IS_MARK_TABLE (self));
  g_return_if_fail (!session || SYSPROF_IS_SESSION (session));

  if (g_set_object (&self->session, session))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SESSION]);
}
