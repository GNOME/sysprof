/*
 * sysprof-expression-map-model.c
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

#include "sysprof-expression-map-model.h"

struct _SysprofExpressionMapModel
{
  GObject parent_instance;

  GListModel *model;
  GtkExpression *expression;

  GSequence *mapped_items; /* NULL if either model or expression are NULL */
};

enum {
  PROP_0,
  PROP_EXPRESSION,
  PROP_MODEL,
  N_PROPS,
};

static GParamSpec *properties [N_PROPS];

typedef struct _ItemData
{
  gpointer mapped_item;
  GtkExpressionWatch *watch;
} ItemData;

static void
item_data_free (gpointer data)
{
  ItemData *item_data = data;

  g_clear_object (&item_data->mapped_item);
  g_clear_pointer (&item_data->watch, gtk_expression_watch_unwatch);
  g_clear_pointer (&item_data, g_free);
}

static ItemData *
item_data_new (gpointer            mapped_item,
               GtkExpressionWatch *watch)
{
  ItemData *item_data;

  item_data = g_new0 (ItemData, 1);
  item_data->mapped_item = mapped_item;
  item_data->watch = watch;

  return item_data;
}

static GType
sysprof_expression_map_model_get_item_type (GListModel *list)
{
  return G_TYPE_OBJECT;
}

static guint
sysprof_expression_map_model_get_n_items (GListModel *list)
{
  SysprofExpressionMapModel *self = SYSPROF_EXPRESSION_MAP_MODEL (list);

  return self->model ? g_list_model_get_n_items (self->model) : 0;
}

static gpointer
sysprof_expression_map_model_get_item (GListModel *list,
                                       guint       position)
{
  SysprofExpressionMapModel *self = SYSPROF_EXPRESSION_MAP_MODEL (list);
  GSequenceIter *iter;
  ItemData *item_data;

  if (!self->model)
    return NULL;

  if (!self->expression)
    return g_list_model_get_item (self->model, position);

  if (position >= g_list_model_get_n_items (self->model))
    return NULL;

  iter = g_sequence_get_iter_at_pos (self->mapped_items, position);
  g_assert (iter != NULL);

  item_data = g_sequence_get (iter);
  g_assert (G_IS_OBJECT (item_data->mapped_item));

  return g_object_ref (item_data->mapped_item);
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = sysprof_expression_map_model_get_item_type;
  iface->get_n_items = sysprof_expression_map_model_get_n_items;
  iface->get_item = sysprof_expression_map_model_get_item;
}

static void
gtk_filter_list_model_get_section (GtkSectionModel *model,
                                   guint            position,
                                   guint           *out_start,
                                   guint           *out_end)
{
  SysprofExpressionMapModel *self = SYSPROF_EXPRESSION_MAP_MODEL (model);

  if (GTK_IS_SECTION_MODEL (self->model))
    {
      gtk_section_model_get_section (GTK_SECTION_MODEL (self->model), position, out_start, out_end);
      return;
    }

  *out_start = 0;
  *out_end = self->model ? g_list_model_get_n_items (self->model) : 0;
}

static void
section_model_iface_init (GtkSectionModelInterface *iface)
{
  iface->get_section = gtk_filter_list_model_get_section;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofExpressionMapModel, sysprof_expression_map_model, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init)
                               G_IMPLEMENT_INTERFACE (GTK_TYPE_SECTION_MODEL, section_model_iface_init))

typedef struct _WatchData
{
  SysprofExpressionMapModel *self;
  gpointer item;
} WatchData;

static void
expression_watch_notify_cb (gpointer data)
{
  SysprofExpressionMapModel *self;
  WatchData *watch_data;
  guint n_items;

  watch_data = data;
  self = watch_data->self;
  n_items = g_list_model_get_n_items (self->model);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(GObject) item = g_list_model_get_item (self->model, i);

      if (item == watch_data->item)
        {
          g_autoptr(GObject) old_mapped_item = NULL;
          g_auto(GValue) value = G_VALUE_INIT;
          GSequenceIter *iter;
          ItemData *item_data;

          iter = g_sequence_get_iter_at_pos (self->mapped_items, i);
          g_assert (iter != NULL);

          item_data = g_sequence_get (iter);
          g_assert (item_data != NULL);

          old_mapped_item = g_steal_pointer (&item_data->mapped_item);

          if (!gtk_expression_evaluate (self->expression, item, &value) ||
              !G_VALUE_HOLDS_OBJECT (&value))
            {
              g_error ("Expression cannot evaluate list item. This is a programming error. Aborting.");
              g_assert_not_reached ();
            }

          item_data->mapped_item = g_value_dup_object (&value);
          g_list_model_items_changed (G_LIST_MODEL (self), i, 1, 1);
          break;
        }
    }
}

static ItemData *
sysprof_expression_map_model_map_and_watch_item (SysprofExpressionMapModel *self,
                                                 guint                      position)
{
  g_autoptr(GObject) item = g_list_model_get_item (self->model, position);
  GtkExpressionWatch *watch = NULL;
  g_auto(GValue) value = G_VALUE_INIT;
  WatchData *watch_data;

  if (!gtk_expression_evaluate (self->expression, item, &value) ||
      !G_VALUE_HOLDS_OBJECT (&value))
    {
      g_error ("Expression cannot evaluate list item. This is a programming error. Aborting.");
      g_assert_not_reached ();
    }

  watch_data = g_new0 (WatchData, 1);
  watch_data->self = self;
  watch_data->item = item;

  watch = gtk_expression_watch (self->expression,
                                item,
                                expression_watch_notify_cb,
                                watch_data,
                                g_free);

  return item_data_new (g_value_dup_object (&value), g_steal_pointer (&watch));
}

static void
sysprof_expression_map_model_items_changed_cb (GListModel                *model,
                                               guint                      position,
                                               guint                      removed,
                                               guint                      added,
                                               SysprofExpressionMapModel *self)
{
  if (self->mapped_items == NULL)
    {
      g_list_model_items_changed (G_LIST_MODEL (self), position, removed, added);
      return;
    }


  if (removed > 0)
    {
      GSequenceIter *iter = g_sequence_get_iter_at_pos (self->mapped_items, position);
      g_sequence_remove_range (iter, g_sequence_iter_move (iter, removed));
    }

  if (added > 0)
    {
      GSequenceIter *iter = g_sequence_get_iter_at_pos (self->mapped_items, position);

      for (guint i = 0; i < added; i++)
        {
          ItemData *item_data = sysprof_expression_map_model_map_and_watch_item (self, position + i);

          g_sequence_insert_before (iter, g_steal_pointer (&item_data));
          iter = g_sequence_iter_next (iter);
        }
    }

  g_list_model_items_changed (G_LIST_MODEL (self), position, removed, added);
}

static void
sysprof_expression_map_model_sections_changed_cb (GtkSectionModel *model,
                                                  unsigned int     position,
                                                  unsigned int     n_items,
                                                  gpointer         user_data)
{
  gtk_section_model_sections_changed (GTK_SECTION_MODEL (user_data), position, n_items);
}

static void
sysprof_expression_map_model_init_items (SysprofExpressionMapModel *self)
{
  if (self->expression && self->model)
    {
      guint n_items;

      if (self->mapped_items)
        {
          g_sequence_remove_range (g_sequence_get_begin_iter (self->mapped_items),
                                   g_sequence_get_end_iter (self->mapped_items));
        }
      else
        {
          self->mapped_items = g_sequence_new (item_data_free);
        }

      n_items = g_list_model_get_n_items (self->model);
      for (guint i = 0; i < n_items; i++)
        {
          g_sequence_append (self->mapped_items,
                             sysprof_expression_map_model_map_and_watch_item (self, i));
        }
    }
  else
    {
      g_clear_pointer (&self->mapped_items, g_sequence_free);
    }
}

static void
sysprof_expression_map_model_clear_model (SysprofExpressionMapModel *self)
{
  if (self->model == NULL)
    return;

  g_signal_handlers_disconnect_by_func (self->model, sysprof_expression_map_model_items_changed_cb, self);
  g_signal_handlers_disconnect_by_func (self->model, sysprof_expression_map_model_sections_changed_cb, self);
  g_clear_object (&self->model);
}

static void
sysprof_expression_map_model_finalize (GObject *object)
{
  SysprofExpressionMapModel *self = (SysprofExpressionMapModel *)object;

  sysprof_expression_map_model_clear_model (self);

  G_OBJECT_CLASS (sysprof_expression_map_model_parent_class)->finalize (object);
}

static void
sysprof_expression_map_model_get_property (GObject    *object,
                                           guint       prop_id,
                                           GValue     *value,
                                           GParamSpec *pspec)
{
  SysprofExpressionMapModel *self = SYSPROF_EXPRESSION_MAP_MODEL (object);

  switch (prop_id)
    {
    case PROP_EXPRESSION:
      gtk_value_set_expression (value, self->expression);
      break;

    case PROP_MODEL:
      g_value_set_object (value, self->model);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_expression_map_model_set_property (GObject      *object,
                                           guint         prop_id,
                                           const GValue *value,
                                           GParamSpec   *pspec)
{
  SysprofExpressionMapModel *self = SYSPROF_EXPRESSION_MAP_MODEL (object);

  switch (prop_id)
    {
    case PROP_EXPRESSION:
      sysprof_expression_map_model_set_expression (self, gtk_value_get_expression (value));
      break;

    case PROP_MODEL:
      sysprof_expression_map_model_set_model (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_expression_map_model_class_init (SysprofExpressionMapModelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_expression_map_model_finalize;
  object_class->get_property = sysprof_expression_map_model_get_property;
  object_class->set_property = sysprof_expression_map_model_set_property;

  properties[PROP_EXPRESSION] =
    gtk_param_spec_expression ("expression", NULL, NULL,
                               (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_MODEL] =
    g_param_spec_object ("model", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_expression_map_model_init (SysprofExpressionMapModel *self)
{

}

SysprofExpressionMapModel *
sysprof_expression_map_model_new (GListModel    *model,
                                  GtkExpression *expression)
{
  g_autoptr(SysprofExpressionMapModel) ret = NULL;

  ret = g_object_new (SYSPROF_TYPE_EXPRESSION_MAP_MODEL,
                      "expression", expression,
                      "model", model,
                      NULL);

  g_clear_pointer (&expression, gtk_expression_unref);
  g_clear_object (&model);

  return g_steal_pointer (&ret);
}

GListModel *
sysprof_expression_map_model_get_model (SysprofExpressionMapModel *self)
{
  g_return_val_if_fail (SYSPROF_IS_EXPRESSION_MAP_MODEL (self), NULL);

  return self->model;
}

void
sysprof_expression_map_model_set_model (SysprofExpressionMapModel *self,
                                        GListModel                *model)
{
  unsigned int removed, added;

  g_return_if_fail (SYSPROF_IS_EXPRESSION_MAP_MODEL (self));
  g_return_if_fail (model == NULL || G_IS_LIST_MODEL (model));

  if (self->model == model)
    return;

  removed = g_list_model_get_n_items (G_LIST_MODEL (self));
  sysprof_expression_map_model_clear_model (self);

  if (model)
    {
      self->model = g_object_ref (model);
      g_signal_connect (model, "items-changed", G_CALLBACK (sysprof_expression_map_model_items_changed_cb), self);
      added = g_list_model_get_n_items (model);

      if (GTK_IS_SECTION_MODEL (model))
        g_signal_connect (model, "sections-changed", G_CALLBACK (sysprof_expression_map_model_sections_changed_cb), self);
    }
  else
    {
      added = 0;
    }

  sysprof_expression_map_model_init_items (self);

  if (removed > 0 || added > 0)
    g_list_model_items_changed (G_LIST_MODEL (self), 0, removed, added);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODEL]);
}

GtkExpression *
sysprof_expression_map_model_get_expression (SysprofExpressionMapModel *self)
{
  g_return_val_if_fail (SYSPROF_IS_EXPRESSION_MAP_MODEL (self), NULL);

  return self->expression;
}

void
sysprof_expression_map_model_set_expression (SysprofExpressionMapModel *self,
                                             GtkExpression             *expression)
{
  g_return_if_fail (SYSPROF_IS_EXPRESSION_MAP_MODEL (self));
  g_return_if_fail (expression == NULL || GTK_IS_EXPRESSION (expression));

  if (self->expression == expression)
    return;

  g_clear_pointer (&self->expression, gtk_expression_unref);
  self->expression = expression ? gtk_expression_ref (expression) : NULL;

  sysprof_expression_map_model_init_items (self);

  if (self->model)
    {
      guint n_items = g_list_model_get_n_items (self->model);
      if (n_items > 0)
        g_list_model_items_changed (G_LIST_MODEL (self), 0, n_items, n_items);
    }

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EXPRESSION]);
}
