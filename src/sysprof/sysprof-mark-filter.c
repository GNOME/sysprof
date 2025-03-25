/*
 * sysprof-mark-filter.c
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

#include "sysprof-mark-filter.h"

typedef struct _SysprofMarkFilterNode SysprofMarkFilterNode;
static void sysprof_mark_filter_node_augment (SysprofMarkFilterNode *node);
#define RB_AUGMENT(elem) (sysprof_mark_filter_node_augment (elem))

#include "tree.h"

struct _SysprofMarkFilterNode
{
  RB_ENTRY (_SysprofMarkFilterNode) link;
  SysprofTimeSpan time_span;
  gint64          max;
};

struct _SysprofMarkFilter
{
  GtkFilter           parent_instance;

  SysprofDocument    *document;
  SysprofMarkCatalog *catalog;

  RB_HEAD (sysprof_mark_filter, _SysprofMarkFilterNode) head;
};

G_DEFINE_FINAL_TYPE (SysprofMarkFilter, sysprof_mark_filter, GTK_TYPE_FILTER)

enum {
  PROP_0,
  PROP_DOCUMENT,
  PROP_CATALOG,
  N_PROPS,
};

static GParamSpec *properties [N_PROPS];

static inline int
sysprof_mark_filter_node_compare (SysprofMarkFilterNode *a,
                                  SysprofMarkFilterNode *b)
{
  if (a->time_span.begin_nsec < b->time_span.begin_nsec)
    return -1;
  else if (a->time_span.begin_nsec > b->time_span.begin_nsec)
    return 1;
  else if (a->time_span.end_nsec < b->time_span.end_nsec)
    return -1;
  else if (a->time_span.end_nsec > b->time_span.end_nsec)
    return -1;
  else
    return 0;
}

RB_GENERATE_STATIC (sysprof_mark_filter, _SysprofMarkFilterNode, link, sysprof_mark_filter_node_compare);

static void
sysprof_mark_filter_node_augment (SysprofMarkFilterNode *node)
{
  node->max = node->time_span.end_nsec;

  if (RB_LEFT (node, link) && RB_LEFT (node, link)->max > node->max)
    node->max = RB_LEFT (node, link)->max;

  if (RB_RIGHT (node, link) && RB_RIGHT (node, link)->max > node->max)
    node->max = RB_RIGHT (node, link)->max;
}

static void
sysprof_mark_filter_node_finalize (SysprofMarkFilterNode *node)
{
  g_free (node);
}

static void
sysprof_mark_filter_node_free (SysprofMarkFilterNode *node)
{
  SysprofMarkFilterNode *right = RB_RIGHT (node, link);
  SysprofMarkFilterNode *left = RB_LEFT (node, link);

  if (left != NULL)
    sysprof_mark_filter_node_free (left);

  sysprof_mark_filter_node_finalize (node);

  if (right != NULL)
    sysprof_mark_filter_node_free (right);
}

static void
populate_intervals (SysprofMarkFilter *self)
{
  unsigned int n_marks;
  GListModel *model;
  int64_t begin_nsec;

  g_assert (SYSPROF_IS_MARK_FILTER (self));
  g_assert (SYSPROF_IS_MARK_CATALOG (self->catalog));
  g_assert (SYSPROF_IS_DOCUMENT (self->document));

  begin_nsec = sysprof_document_get_time_span (self->document)->begin_nsec;

  model = G_LIST_MODEL (self->catalog);
  n_marks = g_list_model_get_n_items (model);

  for (unsigned int i = 0; i < n_marks; i++)
    {
      g_autoptr(SysprofDocumentMark) mark = g_list_model_get_item (model, i);
      SysprofMarkFilterNode *parent;
      SysprofMarkFilterNode *node;
      SysprofMarkFilterNode *ret;
      SysprofTimeSpan time_span;

      g_assert (SYSPROF_IS_DOCUMENT_MARK (mark));

      time_span.begin_nsec = sysprof_document_frame_get_time (SYSPROF_DOCUMENT_FRAME (mark));
      time_span.end_nsec = sysprof_document_mark_get_end_time (mark);

      if (sysprof_time_span_duration (time_span) == 0)
        continue;

      time_span = sysprof_time_span_relative_to (time_span, begin_nsec);

      node = g_new0 (SysprofMarkFilterNode, 1);
      node->time_span = time_span;
      node->max = time_span.end_nsec;

      /* If there is a collision, then the node is returned. Otherwise
       * if the node was inserted, NULL is returned.
       */
      if ((ret = RB_INSERT (sysprof_mark_filter, &self->head, node)))
        {
          sysprof_mark_filter_node_free (node);
          return;
        }

      parent = RB_PARENT (node, link);
      while (parent != NULL)
        {
          if (node->max > parent->max)
            parent->max = node->max;
          node = parent;
          parent = RB_PARENT (parent, link);
        }
    }
}

static gboolean
sysprof_mark_filter_match (GtkFilter *filter,
                           gpointer   item)
{
  SysprofDocumentFrame *frame = (SysprofDocumentFrame *) item;
  SysprofMarkFilter *self = (SysprofMarkFilter *)filter;
  SysprofMarkFilterNode *node;
  int64_t value;

  g_assert (SYSPROF_IS_MARK_FILTER (self));
  g_assert (SYSPROF_IS_DOCUMENT_FRAME (item));

  value = sysprof_document_frame_get_time_offset (frame);
  node = RB_ROOT (&self->head);

  /* The root node contains our calculated max as augmented in RBTree.
   * Therefore, we can know if value falls beyond the upper bound
   * in O(1) without having to add a branch to the while loop below.
   */
  if (node == NULL || node->max < value)
    return FALSE;

  while (node != NULL)
    {
      g_assert (RB_LEFT (node, link) == NULL ||
                node->max >= RB_LEFT (node, link)->max);
      g_assert (RB_RIGHT (node, link) == NULL ||
                node->max >= RB_RIGHT (node, link)->max);

      if (value >= node->time_span.begin_nsec && value <= node->time_span.end_nsec)
        return TRUE;

      if (RB_LEFT (node, link) && RB_LEFT (node, link)->max >= value)
        node = RB_LEFT (node, link);
      else
        node = RB_RIGHT (node, link);
    }

  return FALSE;
}

static void
sysprof_mark_filter_finalize (GObject *object)
{
  SysprofMarkFilter *self = (SysprofMarkFilter *)object;
  SysprofMarkFilterNode *node = RB_ROOT(&self->head);

  g_clear_object (&self->catalog);

  if (node != NULL)
    sysprof_mark_filter_node_free (node);

  G_OBJECT_CLASS (sysprof_mark_filter_parent_class)->finalize (object);
}

static void
sysprof_mark_filter_constructed (GObject *object)
{
  SysprofMarkFilter *self = (SysprofMarkFilter *)object;

  G_OBJECT_CLASS (sysprof_mark_filter_parent_class)->constructed (object);

  populate_intervals (self);
}

static void
sysprof_mark_filter_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  SysprofMarkFilter *self = SYSPROF_MARK_FILTER (object);

  switch (prop_id)
    {
    case PROP_DOCUMENT:
      g_value_set_object (value, self->document);
      break;

    case PROP_CATALOG:
      g_value_set_object (value, self->catalog);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_mark_filter_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  SysprofMarkFilter *self = SYSPROF_MARK_FILTER (object);

  switch (prop_id)
    {
    case PROP_CATALOG:
      g_assert (self->catalog == NULL);
      self->catalog = g_value_dup_object (value);
      break;

    case PROP_DOCUMENT:
      g_assert (self->document == NULL);
      self->document = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_mark_filter_class_init (SysprofMarkFilterClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkFilterClass *filter_class = GTK_FILTER_CLASS (klass);

  object_class->finalize = sysprof_mark_filter_finalize;
  object_class->constructed = sysprof_mark_filter_constructed;
  object_class->get_property = sysprof_mark_filter_get_property;
  object_class->set_property = sysprof_mark_filter_set_property;

  filter_class->match = sysprof_mark_filter_match;

  properties[PROP_CATALOG] =
    g_param_spec_object ("catalog", NULL, NULL,
                         SYSPROF_TYPE_MARK_CATALOG,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties[PROP_DOCUMENT] =
    g_param_spec_object ("document", NULL, NULL,
                         SYSPROF_TYPE_DOCUMENT,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_mark_filter_init (SysprofMarkFilter *self)
{
  RB_INIT (&self->head);
}

SysprofMarkFilter *
sysprof_mark_filter_new (SysprofDocument    *document,
                         SysprofMarkCatalog *catalog)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (document), NULL);
  g_return_val_if_fail (SYSPROF_IS_MARK_CATALOG (catalog), NULL);

  return g_object_new (SYSPROF_TYPE_MARK_FILTER,
                       "catalog", catalog,
                       "document", document,
                       NULL);
}

SysprofMarkCatalog *
sysprof_mark_filter_get_catalog (SysprofMarkFilter *self)
{
  g_return_val_if_fail (SYSPROF_IS_MARK_FILTER (self), NULL);

  return self->catalog;
}
