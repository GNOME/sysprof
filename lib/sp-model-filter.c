/* sp-model-filter.c
 *
 * Copyright (C) 2016 Christian Hergert <christian@hergert.me>
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
 */

#include "sp-model-filter.h"

/*
 * This is a simple model filter for GListModel.
 *
 * Let me start by saying how it works, and then how I wish it worked.
 *
 * This uses 2 GSequence (Treaps) to track the filters. One matches the
 * underlying listmodel. One matches the filtered set. We hold onto our
 * own reference to the object in the child list model, and update things
 * as necessary when ::items-changed is emitted.
 *
 * I'd rather see this solved in one of two ways.
 *
 * 1) Add filtering support to GListStore
 *
 * or
 *
 * 2) Create a multi-tree data-structure that contains two tree nodes
 *    in each element. One tree contains all items, one tree contains
 *    the visible items (a subset of the other tree). The nodes might
 *    look something like:
 *
 *    Item {
 *      TreeNode  all_tree;
 *      TreeNode  visible_tree;
 *      GObject  *item;
 *    }
 *
 * But either way, this gets the job done for now. I'd venture a guess
 * that in many cases (small lists), this is actually slower than just
 * rechecking a simple GPtrArray, but let's see how it goes.
 *
 * -- Christian
 */

typedef struct
{
  GListModel        *child_model;

  GSequence         *seq;
  GSequence         *visible_seq;

  SpModelFilterFunc  filter_func;
  gpointer           filter_func_data;
  GDestroyNotify     filter_func_data_destroy;

  guint              needs_rebuild : 1;
} SpModelFilterPrivate;

typedef struct
{
  GSequenceIter *iter;
  GObject       *object;
} Element;

static void list_model_iface_init (GListModelInterface *iface);

G_DEFINE_TYPE_EXTENDED (SpModelFilter, sp_model_filter, G_TYPE_OBJECT, 0,
                        G_ADD_PRIVATE (SpModelFilter)
                        G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL,
                                               list_model_iface_init))

enum {
  PROP_0,
  PROP_CHILD_MODEL,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

static void
element_free (gpointer data)
{
  Element *e = data;

  g_clear_object (&e->object);
  g_slice_free (Element, e);
}

static gboolean
sp_model_filter_default_filter_func (GObject  *item,
                                     gpointer  user_data)
{
  return TRUE;
}

static void
sp_model_filter_child_model_items_changed (SpModelFilter *self,
                                           guint          position,
                                           guint          n_removed,
                                           guint          n_added,
                                           GListModel    *child_model)
{
  SpModelFilterPrivate *priv = sp_model_filter_get_instance_private (self);
  GSequenceIter *insert_before = NULL;
  GSequenceIter *insert_iter;
  GSequenceIter *lower;
  GSequenceIter *upper;
  guint i;

  g_assert (SP_IS_MODEL_FILTER (self));
  g_assert (G_IS_LIST_MODEL (child_model));


  for (i = 0; i < n_removed; i++)
    {
      GSequenceIter *iter;
      Element *ele;

      iter = g_sequence_get_iter_at_pos (priv->seq, position);
      ele = g_sequence_get (iter);

      if (ele->iter)
        {
          guint visible_position = g_sequence_iter_get_position (ele->iter);

          insert_before = g_sequence_iter_next (ele->iter);
          g_sequence_remove (ele->iter);
          g_list_model_items_changed (G_LIST_MODEL (self), visible_position, 1, 0);
        }

      g_sequence_remove (iter);
    }

  insert_iter = g_sequence_get_iter_at_pos (priv->seq, position + 1);

  if (insert_before != NULL)
    goto add_items;

#if GLIB_CHECK_VERSION(2, 48, 0)
  if (g_sequence_is_empty (priv->visible_seq))
#else
  if (g_sequence_get_begin_iter (priv->visible_seq) == g_sequence_get_end_iter (priv->visible_seq))
#endif
    {
      insert_before = g_sequence_get_end_iter (priv->visible_seq);
      goto add_items;
    }

  lower = g_sequence_get_begin_iter (priv->visible_seq);
  upper = g_sequence_get_end_iter (priv->visible_seq);

  while (lower != upper)
    {
      GSequenceIter *mid;
      GSequenceIter *iter;
      guint mid_pos;

      mid = g_sequence_range_get_midpoint (lower, upper);
      iter = g_sequence_get (mid);
      mid_pos = g_sequence_iter_get_position (iter);

      if (mid_pos < position)
        lower = g_sequence_iter_next (mid);
      else if (mid_pos > position)
        upper = g_sequence_iter_prev (mid);
      else
        upper = lower = mid;
    }

  if (upper == g_sequence_get_end_iter (priv->visible_seq))
    insert_before = upper;
  else
    insert_before =
      ((guint)g_sequence_iter_get_position (g_sequence_get (upper)) <= position)
        ? upper : g_sequence_iter_next (upper);

add_items:
  for (i = 0; i < n_added; i++)
    {
      GSequenceIter *iter;
      Element *ele;

      ele = g_slice_new (Element);
      ele->object = g_list_model_get_item (priv->child_model, position + i);
      ele->iter = NULL;

      iter = g_sequence_insert_before (insert_iter, ele);

      if (priv->filter_func (ele->object, priv->filter_func_data))
        {
          ele->iter = g_sequence_insert_before (insert_before, iter);
          g_list_model_items_changed (G_LIST_MODEL (self),
                                      g_sequence_iter_get_position (ele->iter),
                                      0, 1);
        }
    }
}

static void
sp_model_filter_finalize (GObject *object)
{
  SpModelFilter *self = (SpModelFilter *)object;
  SpModelFilterPrivate *priv = sp_model_filter_get_instance_private (self);

  g_clear_pointer (&priv->seq, g_sequence_free);
  g_clear_pointer (&priv->visible_seq, g_sequence_free);

  if (priv->filter_func_data_destroy)
    {
      g_clear_pointer (&priv->filter_func_data, priv->filter_func_data_destroy);
      priv->filter_func_data_destroy = NULL;
    }

  g_clear_object (&priv->child_model);

  G_OBJECT_CLASS (sp_model_filter_parent_class)->finalize (object);
}

static void
sp_model_filter_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  SpModelFilter *self = SP_MODEL_FILTER (object);

  switch (prop_id)
    {
    case PROP_CHILD_MODEL:
      g_value_set_object (value, sp_model_filter_get_child_model (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_model_filter_class_init (SpModelFilterClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sp_model_filter_finalize;
  object_class->get_property = sp_model_filter_get_property;

  properties [PROP_CHILD_MODEL] =
    g_param_spec_object ("child-model",
                         "Child Model",
                         "The child model being filtered.",
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sp_model_filter_init (SpModelFilter *self)
{
  SpModelFilterPrivate *priv = sp_model_filter_get_instance_private (self);

  priv->filter_func = sp_model_filter_default_filter_func;
  priv->seq = g_sequence_new (element_free);
  priv->visible_seq = g_sequence_new (NULL);

  priv->needs_rebuild = TRUE;
}

static void
sp_model_filter_rebuild (SpModelFilter *self,
                         gboolean       no_emit)
{
  SpModelFilterPrivate *priv = sp_model_filter_get_instance_private (self);
  guint new_n_items = 0;
  guint old_n_items;
  guint n_items;
  guint i;

  g_assert (SP_IS_MODEL_FILTER (self));
  g_assert (priv->needs_rebuild);

  old_n_items = g_sequence_get_length (priv->visible_seq);

  g_clear_pointer (&priv->seq, g_sequence_free);
  g_clear_pointer (&priv->visible_seq, g_sequence_free);

  priv->seq = g_sequence_new (element_free);
  priv->visible_seq = g_sequence_new (NULL);

  n_items = g_list_model_get_n_items (priv->child_model);

  for (i = 0; i < n_items; i++)
    {
      GSequenceIter *iter;
      Element *ele;

      ele = g_slice_new (Element);
      ele->object = g_list_model_get_item (priv->child_model, i);
      ele->iter = NULL;

      iter = g_sequence_append (priv->seq, ele);

      if (priv->filter_func (ele->object, priv->filter_func_data))
        {
          ele->iter = g_sequence_append (priv->visible_seq, iter);
          new_n_items++;
        }
    }

  if (!no_emit)
    g_list_model_items_changed (G_LIST_MODEL (self), 0, old_n_items, new_n_items);

  priv->needs_rebuild = FALSE;
}

static GType
sp_model_filter_get_item_type (GListModel *model)
{
  SpModelFilter *self = (SpModelFilter *)model;
  SpModelFilterPrivate *priv = sp_model_filter_get_instance_private (self);

  g_assert (SP_IS_MODEL_FILTER (self));

  return g_list_model_get_item_type (priv->child_model);
}

static guint
sp_model_filter_get_n_items (GListModel *model)
{
  SpModelFilter *self = (SpModelFilter *)model;
  SpModelFilterPrivate *priv = sp_model_filter_get_instance_private (self);

  g_assert (SP_IS_MODEL_FILTER (self));

  if (priv->needs_rebuild)
    sp_model_filter_rebuild (self, TRUE);

  return g_sequence_get_length (priv->visible_seq);
}

static gpointer
sp_model_filter_get_item (GListModel *model,
                          guint       position)
{
  SpModelFilter *self = (SpModelFilter *)model;
  SpModelFilterPrivate *priv = sp_model_filter_get_instance_private (self);
  GSequenceIter *iter;
  Element *ele;

  g_assert (SP_IS_MODEL_FILTER (self));

  if (priv->needs_rebuild)
    sp_model_filter_rebuild (self, TRUE);

  iter = g_sequence_get_iter_at_pos (priv->visible_seq, position);

  if (!iter || g_sequence_iter_is_end (iter))
    {
      g_warning ("invalid position for filter, filter is corrupt");
      return NULL;
    }

  iter = g_sequence_get (iter);

  if (!iter || g_sequence_iter_is_end (iter))
    {
      g_warning ("invalid position for filter, filter is corrupt");
      return NULL;
    }

  ele = g_sequence_get (iter);

  return g_object_ref (ele->object);
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = sp_model_filter_get_item_type;
  iface->get_n_items = sp_model_filter_get_n_items;
  iface->get_item = sp_model_filter_get_item;
}

SpModelFilter *
sp_model_filter_new (GListModel *child_model)
{
  SpModelFilter *ret;
  SpModelFilterPrivate *priv;

  g_return_val_if_fail (G_IS_LIST_MODEL (child_model), NULL);

  ret = g_object_new (SP_TYPE_MODEL_FILTER, NULL);
  priv = sp_model_filter_get_instance_private (ret);
  priv->child_model = g_object_ref (child_model);

  g_signal_connect_object (child_model,
                           "items-changed",
                           G_CALLBACK (sp_model_filter_child_model_items_changed),
                           ret,
                           G_CONNECT_SWAPPED);

  return ret;
}

/**
 * sp_model_filter_get_child_model:
 * @self: A #SpModelFilter
 *
 * Gets the child model that is being filtered.
 *
 * Returns: (transfer none): A #GListModel.
 */
GListModel *
sp_model_filter_get_child_model (SpModelFilter *self)
{
  SpModelFilterPrivate *priv = sp_model_filter_get_instance_private (self);

  g_return_val_if_fail (SP_IS_MODEL_FILTER (self), NULL);

  return priv->child_model;
}

void
sp_model_filter_invalidate (SpModelFilter *self)
{
  SpModelFilterPrivate *priv = sp_model_filter_get_instance_private (self);

  g_return_if_fail (SP_IS_MODEL_FILTER (self));

  priv->needs_rebuild = TRUE;

  sp_model_filter_rebuild (self, FALSE);
}

void
sp_model_filter_set_filter_func (SpModelFilter     *self,
                                 SpModelFilterFunc  filter_func,
                                 gpointer           filter_func_data,
                                 GDestroyNotify     filter_func_data_destroy)
{
  SpModelFilterPrivate *priv = sp_model_filter_get_instance_private (self);

  g_return_if_fail (SP_IS_MODEL_FILTER (self));
  g_return_if_fail (filter_func || (!filter_func_data && !filter_func_data_destroy));

  if (priv->filter_func_data_destroy)
    g_clear_pointer (&priv->filter_func_data, priv->filter_func_data_destroy);

  if (filter_func != NULL)
    {
      priv->filter_func = filter_func;
      priv->filter_func_data = filter_func_data;
      priv->filter_func_data_destroy = filter_func_data_destroy;
    }
  else
    {
      priv->filter_func = sp_model_filter_default_filter_func;
      priv->filter_func_data = NULL;
      priv->filter_func_data_destroy = NULL;
    }

  sp_model_filter_invalidate (self);
}
