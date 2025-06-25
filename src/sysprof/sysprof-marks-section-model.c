/*
 * sysprof-marks-section-model.c
 *
 * Copyright 2025 Georges Basile Stavracas Neto <feaneron@igalia.com>
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

#include "sysprof-marks-section-model.h"

#include "sysprof-expression-map-model.h"
#include "sysprof-marks-section-model-item.h"

struct _SysprofMarksSectionModel
{
  GObject parent_instance;

  SysprofSession *session;

  GListModel     *groups;
  GListModel     *filtered_groups;

  GListModel     *catalog;
  GListModel     *filtered_catalog;

  GListModel     *marks;
  GListModel     *filtered_marks;
};

static GType
sysprof_marks_section_model_get_item_type (GListModel *model)
{
  return G_TYPE_OBJECT;
}

static gpointer
sysprof_marks_section_model_get_item (GListModel *model,
                                      guint       position)
{
  SysprofMarksSectionModel *self = (SysprofMarksSectionModel *) model;

  if (self->filtered_marks == NULL)
    return NULL;

  return g_list_model_get_item (self->filtered_marks, position);
}

static guint
sysprof_marks_section_model_get_n_items (GListModel *model)
{
  SysprofMarksSectionModel *self = (SysprofMarksSectionModel *) model;

  if (self->filtered_marks == NULL)
    return 0;

  return g_list_model_get_n_items (self->filtered_marks);
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = sysprof_marks_section_model_get_item_type;
  iface->get_n_items = sysprof_marks_section_model_get_n_items;
  iface->get_item = sysprof_marks_section_model_get_item;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofMarksSectionModel, sysprof_marks_section_model, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

enum {
  PROP_0,
  PROP_FILTERED_GROUPS,
  PROP_FILTERED_MARK_CATALOG,
  PROP_FILTERED_MARKS,
  PROP_GROUPS,
  PROP_MARK_CATALOG,
  PROP_MARKS,
  PROP_SESSION,
  N_PROPS,
};

static GParamSpec *properties [N_PROPS];

static void
sysprof_marks_section_model_disconnect (SysprofMarksSectionModel *self)
{
  g_assert (SYSPROF_IS_MARKS_SECTION_MODEL (self));
  g_assert (SYSPROF_IS_SESSION (self->session));

  g_clear_object (&self->catalog);
  g_clear_object (&self->filtered_catalog);
  g_clear_object (&self->filtered_groups);
  g_clear_object (&self->filtered_marks);
  g_clear_object (&self->groups);
  g_clear_object (&self->marks);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILTERED_GROUPS]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILTERED_MARK_CATALOG]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILTERED_MARKS]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GROUPS]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MARK_CATALOG]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MARKS]);
}

static gpointer
map_func (gpointer item)
{
  return sysprof_marks_section_model_item_new (item);
}

static inline GListModel *
create_filter_model (GListModel *model,
                     GtkFilter  *filter)
{
  g_autoptr(GtkFilterListModel) filter_model = NULL;

  filter_model = gtk_filter_list_model_new (model, filter);
  gtk_filter_list_model_set_watch_items (filter_model, TRUE);
  gtk_filter_list_model_set_incremental (filter_model, TRUE);

  return G_LIST_MODEL (g_steal_pointer (&filter_model));
}

static void
sysprof_marks_section_model_connect (SysprofMarksSectionModel *self)
{
  g_autoptr(GtkExpression) expression = NULL;
  SysprofDocument *document;

  g_assert (SYSPROF_IS_MARKS_SECTION_MODEL (self));
  g_assert (SYSPROF_IS_SESSION (self->session));

  document = sysprof_session_get_document (self->session);

#define MAP(_model) (G_LIST_MODEL (sysprof_expression_map_model_new (G_LIST_MODEL ((_model)), \
                                                                     GTK_EXPRESSION (gtk_closure_expression_new (G_TYPE_OBJECT, g_cclosure_new (G_CALLBACK (map_func), NULL, NULL), 0, NULL)))))
#define FLATTEN(_model) (G_LIST_MODEL (gtk_flatten_list_model_new (G_LIST_MODEL ((_model)))))
#define FILTER(_model, _filter) (create_filter_model (G_LIST_MODEL (_model), GTK_FILTER (_filter)))
#define SORT(_model, _sorter) (G_LIST_MODEL (gtk_sort_list_model_new (G_LIST_MODEL ((_model)), GTK_SORTER ((_sorter)))))

  self->groups = MAP (sysprof_document_catalog_marks (document));

  /* Turns groups into SysprofMarksSectionModelItem<SysprofMarkCatalogs> */
  self->catalog = MAP (FLATTEN (g_object_ref (self->groups)));

  /* Raw marks */
  self->marks = g_object_ref (sysprof_document_list_marks (document));

  /* Filter groups based on SysprofMarksSectionModelItem.visible */
  expression = gtk_property_expression_new (SYSPROF_TYPE_MARKS_SECTION_MODEL_ITEM, NULL, "visible");
  self->filtered_groups = FILTER (g_object_ref (self->groups), gtk_bool_filter_new (g_steal_pointer (&expression)));

  /* Filter SysprofMarksSectionModelItem<SysprofMarkCatalogs> based on the visible property */
  expression = gtk_property_expression_new (SYSPROF_TYPE_MARKS_SECTION_MODEL_ITEM, NULL, "visible");
  self->filtered_catalog = FILTER (g_object_ref (self->catalog), gtk_bool_filter_new (g_steal_pointer (&expression)));

  expression = gtk_property_expression_new (SYSPROF_TYPE_DOCUMENT_MARK, NULL, "time");
  self->filtered_marks = SORT (FLATTEN (g_object_ref (self->filtered_catalog)),
                               gtk_numeric_sorter_new (g_steal_pointer (&expression)));

  g_signal_connect_swapped (self->filtered_marks, "items-changed", G_CALLBACK (g_list_model_items_changed), self);
  g_list_model_items_changed (G_LIST_MODEL (self), 0, 0, g_list_model_get_n_items (self->filtered_marks));

#undef MAP
#undef FLATTEN
#undef FILTER
#undef SORT

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILTERED_GROUPS]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILTERED_MARK_CATALOG]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILTERED_MARKS]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GROUPS]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MARK_CATALOG]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MARKS]);
}

static void
sysprof_marks_section_model_dispose (GObject *object)
{
  SysprofMarksSectionModel *self = (SysprofMarksSectionModel *)object;

  if (self->session)
    {
      sysprof_marks_section_model_disconnect (self);
      g_clear_object (&self->session);
    }

  G_OBJECT_CLASS (sysprof_marks_section_model_parent_class)->dispose (object);
}

static void
sysprof_marks_section_model_get_property (GObject    *object,
                                          guint       prop_id,
                                          GValue     *value,
                                          GParamSpec *pspec)
{
  SysprofMarksSectionModel *self = SYSPROF_MARKS_SECTION_MODEL (object);

  switch (prop_id)
    {
    case PROP_FILTERED_GROUPS:
      g_value_set_object (value, self->filtered_groups);
      break;

    case PROP_FILTERED_MARK_CATALOG:
      g_value_set_object (value, self->filtered_catalog);
      break;

    case PROP_FILTERED_MARKS:
      g_value_set_object (value, self->filtered_marks);
      break;

    case PROP_GROUPS:
      g_value_set_object (value, self->groups);
      break;

    case PROP_MARK_CATALOG:
      g_value_set_object (value, self->catalog);
      break;

    case PROP_MARKS:
      g_value_set_object (value, self->marks);
      break;

    case PROP_SESSION:
      g_value_set_object (value, sysprof_marks_section_model_get_session (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_marks_section_model_set_property (GObject      *object,
                                          guint         prop_id,
                                          const GValue *value,
                                          GParamSpec   *pspec)
{
  SysprofMarksSectionModel *self = SYSPROF_MARKS_SECTION_MODEL (object);

  switch (prop_id)
    {
    case PROP_SESSION:
      sysprof_marks_section_model_set_session (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_marks_section_model_class_init (SysprofMarksSectionModelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = sysprof_marks_section_model_dispose;
  object_class->get_property = sysprof_marks_section_model_get_property;
  object_class->set_property = sysprof_marks_section_model_set_property;

  properties [PROP_FILTERED_GROUPS] =
    g_param_spec_object ("filtered-groups", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_FILTERED_MARK_CATALOG] =
    g_param_spec_object ("filtered-mark-catalog", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_FILTERED_MARKS] =
    g_param_spec_object ("filtered-marks", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_GROUPS] =
    g_param_spec_object ("groups", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_MARK_CATALOG] =
    g_param_spec_object ("mark-catalog", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_MARKS] =
    g_param_spec_object ("marks", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SESSION] =
    g_param_spec_object ("session", NULL, NULL,
                         SYSPROF_TYPE_SESSION,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_marks_section_model_init (SysprofMarksSectionModel *self)
{
}

SysprofSession *
sysprof_marks_section_model_get_session (SysprofMarksSectionModel *self)
{
  g_return_val_if_fail (SYSPROF_IS_MARKS_SECTION_MODEL (self), NULL);

  return self->session;
}

void
sysprof_marks_section_model_set_session (SysprofMarksSectionModel *self,
                                         SysprofSession           *session)
{
  g_return_if_fail (SYSPROF_IS_MARKS_SECTION_MODEL (self));
  g_return_if_fail (!session || SYSPROF_IS_SESSION (session));

  if (self->session == session)
    return;

  if (self->session)
    sysprof_marks_section_model_disconnect (self);

  g_set_object (&self->session, session);

  if (session)
    sysprof_marks_section_model_connect (self);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SESSION]);
}

GListModel *
sysprof_marks_section_model_get_groups (SysprofMarksSectionModel *self)
{
  g_return_val_if_fail (SYSPROF_IS_MARKS_SECTION_MODEL (self), NULL);

  return self->groups;
}

GListModel *
sysprof_marks_section_model_get_filtered_groups (SysprofMarksSectionModel *self)
{
  g_return_val_if_fail (SYSPROF_IS_MARKS_SECTION_MODEL (self), NULL);

  return self->filtered_groups;
}

GListModel *
sysprof_marks_section_model_get_mark_catalog (SysprofMarksSectionModel *self)
{
  g_return_val_if_fail (SYSPROF_IS_MARKS_SECTION_MODEL (self), NULL);

  return self->catalog;
}

GListModel *
sysprof_marks_section_model_get_filtered_mark_catalog (SysprofMarksSectionModel *self)
{
  g_return_val_if_fail (SYSPROF_IS_MARKS_SECTION_MODEL (self), NULL);

  return self->filtered_catalog;
}

GListModel *
sysprof_marks_section_model_get_marks (SysprofMarksSectionModel *self)
{
  g_return_val_if_fail (SYSPROF_IS_MARKS_SECTION_MODEL (self), NULL);

  return self->marks;
}

GListModel *
sysprof_marks_section_model_get_filtered_marks (SysprofMarksSectionModel *self)
{
  g_return_val_if_fail (SYSPROF_IS_MARKS_SECTION_MODEL (self), NULL);

  return self->filtered_marks;
}
