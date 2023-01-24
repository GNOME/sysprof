/* sysprof-callgraph-page.c
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
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

/* Sysprof -- Sampling, systemwide CPU profiler
 * Copyright 2004, Red Hat, Inc.
 * Copyright 2004, 2005, 2006, Soeren Sandmann
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <glib/gi18n.h>

#include "../stackstash.h"

#include "egg-paned-private.h"

#include "sysprof-callgraph-page.h"
#include "sysprof-cell-renderer-percent.h"
#include "sysprof-stack-node-item.h"
#include "sysprof-percent-label.h"

typedef struct
{
  SysprofCallgraphProfile  *profile;

  GtkTreeView              *callers_view;
  GtkColumnView            *functions_view;
  GtkSingleSelection       *functions_selection_model;
  GtkSortListModel         *functions_sort_model;
  GtkColumnViewColumn      *function_total_column;
  GtkTreeView              *descendants_view;
  GtkTreeViewColumn        *descendants_name_column;
  GtkStack                 *stack;
  GtkWidget                *empty_state;
  GtkWidget                *loading_state;
  GtkWidget                *callgraph;

  GQueue                   *history;

  guint                     profile_size;
  guint                     loading;
} SysprofCallgraphPagePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (SysprofCallgraphPage, sysprof_callgraph_page, SYSPROF_TYPE_PAGE)

enum {
  PROP_0,
  PROP_PROFILE,
  N_PROPS
};

enum {
  GO_PREVIOUS,
  N_SIGNALS
};

enum {
  COLUMN_NAME,
  COLUMN_SELF,
  COLUMN_TOTAL,
  COLUMN_POINTER,
  COLUMN_HITS,
};

static void sysprof_callgraph_page_update_descendants (SysprofCallgraphPage *self,
                                                       StackNode            *node);

static GParamSpec *properties [N_PROPS];
static guint signals [N_SIGNALS];

static guint
sysprof_callgraph_page_get_profile_size (SysprofCallgraphPage *self)
{
  SysprofCallgraphPagePrivate *priv = sysprof_callgraph_page_get_instance_private (self);
  StackStash *stash;
  StackNode *node;
  guint size = 0;

  g_assert (SYSPROF_IS_CALLGRAPH_PAGE (self));

  if (priv->profile_size != 0)
    return priv->profile_size;

  if (priv->profile == NULL)
    return 0;

  if (NULL == (stash = sysprof_callgraph_profile_get_stash (priv->profile)))
    return 0;

  for (node = stack_stash_get_root (stash); node != NULL; node = node->siblings)
    size += node->total;

  priv->profile_size = size;

  return size;
}

static void
build_functions_store (StackNode *node,
                       gpointer   user_data)
{
  struct {
    GListStore *store;
    gdouble profile_size;
  } *state = user_data;
  SysprofStackNodeItem *item;

  g_assert (state != NULL);
  g_assert (G_IS_LIST_STORE (state->store));

  item = sysprof_stack_node_item_new (node, state->profile_size);
  g_list_store_append (state->store, item);
}

static void
sysprof_callgraph_page_load (SysprofCallgraphPage    *self,
                             SysprofCallgraphProfile *profile)
{
  SysprofCallgraphPagePrivate *priv = sysprof_callgraph_page_get_instance_private (self);
  GListStore *functions;
  StackStash *stash;
  StackNode *n;
  struct {
    GListStore *store;
    gdouble profile_size;
  } state = { 0 };

  g_assert (SYSPROF_IS_CALLGRAPH_PAGE (self));
  g_assert (SYSPROF_IS_CALLGRAPH_PROFILE (profile));

  /*
   * TODO: This is probably the type of thing we want to do off the main
   *       thread. We should be able to build the tree models off thread
   *       and then simply apply them on the main thread.
   *
   *       In the mean time, we should set the state of the widget to
   *       insensitive and give some indication of loading progress.
   */

  if (!g_set_object (&priv->profile, profile))
    return;

  if (sysprof_callgraph_profile_is_empty (profile))
    return;

  stash = sysprof_callgraph_profile_get_stash (profile);

  for (n = stack_stash_get_root (stash); n; n = n->siblings)
    state.profile_size += n->total;

  functions = g_list_store_new (SYSPROF_TYPE_STACK_NODE_ITEM);
  state.store = functions;
  stack_stash_foreach_by_address (stash, build_functions_store, &state);

  gtk_column_view_sort_by_column (priv->functions_view,
                                  priv->function_total_column,
                                  GTK_SORT_DESCENDING);

  gtk_sort_list_model_set_model (priv->functions_sort_model,
                                 G_LIST_MODEL (functions));
  gtk_tree_view_set_model (priv->callers_view, NULL);
  gtk_tree_view_set_model (priv->descendants_view, NULL);

  gtk_single_selection_set_selected (priv->functions_selection_model, 0);

  gtk_stack_set_visible_child (priv->stack, priv->callgraph);

  g_clear_object (&functions);
}

void
_sysprof_callgraph_page_set_failed (SysprofCallgraphPage *self)
{
  SysprofCallgraphPagePrivate *priv = sysprof_callgraph_page_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_CALLGRAPH_PAGE (self));

  gtk_stack_set_visible_child (priv->stack, priv->empty_state);
}

static void
sysprof_callgraph_page_unload (SysprofCallgraphPage *self)
{
  SysprofCallgraphPagePrivate *priv = sysprof_callgraph_page_get_instance_private (self);

  g_assert (SYSPROF_IS_CALLGRAPH_PAGE (self));
  g_assert (SYSPROF_IS_CALLGRAPH_PROFILE (priv->profile));

  g_queue_clear (priv->history);
  g_clear_object (&priv->profile);
  priv->profile_size = 0;

  gtk_tree_view_set_model (priv->callers_view, NULL);
  gtk_sort_list_model_set_model (priv->functions_sort_model, NULL);
  gtk_tree_view_set_model (priv->descendants_view, NULL);

  gtk_stack_set_visible_child (priv->stack, priv->empty_state);
}

/**
 * sysprof_callgraph_page_get_profile:
 *
 * Returns: (transfer none): An #SysprofCallgraphProfile.
 */
SysprofCallgraphProfile *
sysprof_callgraph_page_get_profile (SysprofCallgraphPage *self)
{
  SysprofCallgraphPagePrivate *priv = sysprof_callgraph_page_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH_PAGE (self), NULL);

  return priv->profile;
}

void
sysprof_callgraph_page_set_profile (SysprofCallgraphPage    *self,
                                    SysprofCallgraphProfile *profile)
{
  SysprofCallgraphPagePrivate *priv = sysprof_callgraph_page_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_CALLGRAPH_PAGE (self));
  g_return_if_fail (!profile || SYSPROF_IS_CALLGRAPH_PROFILE (profile));

  if (profile != priv->profile)
    {
      if (priv->profile)
        sysprof_callgraph_page_unload (self);

      if (profile)
        sysprof_callgraph_page_load (self, profile);

      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_PROFILE]);
    }
}

static void
sysprof_callgraph_page_expand_descendants (SysprofCallgraphPage *self)
{
  SysprofCallgraphPagePrivate *priv = sysprof_callgraph_page_get_instance_private (self);
  GtkTreeModel *model;
  GList *all_paths = NULL;
  GtkTreePath *first_path;
  GtkTreeIter iter;
  gdouble top_value = 0;
  gint max_rows = 40; /* FIXME */
  gint n_rows;

  g_assert (SYSPROF_IS_CALLGRAPH_PAGE (self));

  model = gtk_tree_view_get_model (priv->descendants_view);
  first_path = gtk_tree_path_new_first ();
  all_paths = g_list_prepend (all_paths, first_path);
  n_rows = 1;

  gtk_tree_model_get_iter (model, &iter, first_path);
  gtk_tree_model_get (model, &iter,
                      COLUMN_TOTAL, &top_value,
                      -1);

  while ((all_paths != NULL) && (n_rows < max_rows))
    {
      GtkTreeIter best_iter;
      GtkTreePath *best_path = NULL;
      GList *list;
      gdouble best_value = 0.0;
      gint n_children;
      gint i;

      for (list = all_paths; list != NULL; list = list->next)
        {
          GtkTreePath *path = list->data;

          g_assert (path != NULL);

          if (gtk_tree_model_get_iter (model, &iter, path))
            {
              gdouble value;

              gtk_tree_model_get (model, &iter,
                                  COLUMN_TOTAL, &value,
                                  -1);

              if (value >= best_value)
                {
                  best_value = value;
                  best_path = path;
                  best_iter = iter;
                }
            }
        }

      n_children = gtk_tree_model_iter_n_children (model, &best_iter);

      if ((n_children > 0) &&
          ((best_value / top_value) > 0.04) &&
          ((n_children + gtk_tree_path_get_depth (best_path)) / (gdouble)max_rows) < (best_value / top_value))
        {
          gtk_tree_view_expand_row (priv->descendants_view, best_path, FALSE);
          n_rows += n_children;

          if (gtk_tree_path_get_depth (best_path) < 4)
            {
              GtkTreePath *path;

              path = gtk_tree_path_copy (best_path);
              gtk_tree_path_down (path);

              for (i = 0; i < n_children; i++)
                {
                  all_paths = g_list_prepend (all_paths, path);

                  path = gtk_tree_path_copy (path);
                  gtk_tree_path_next (path);
                }

              gtk_tree_path_free (path);
            }
        }

      all_paths = g_list_remove (all_paths, best_path);

      /* Always expand at least once */
      if ((all_paths == NULL) && (n_rows == 1))
        gtk_tree_view_expand_row (priv->descendants_view, best_path, FALSE);

      gtk_tree_path_free (best_path);
    }

  g_list_free_full (all_paths, (GDestroyNotify)gtk_tree_path_free);
}

typedef struct
{
  StackNode   *node;
  const gchar *name;
  guint        self;
  guint        total;
} Caller;

static Caller *
caller_new (StackNode *node)
{
  Caller *c;

  c = g_slice_new (Caller);
  c->name = U64_TO_POINTER (node->data);
  c->self = 0;
  c->total = 0;
  c->node = node;

  return c;
}

static void
caller_free (gpointer data)
{
  Caller *c = data;
  g_slice_free (Caller, c);
}

static void
sysprof_callgraph_page_function_selection_changed (SysprofCallgraphPage *self,
                                                   guint                 position,
                                                   guint                 n_items,
                                                   GtkSelectionModel    *selection)
{
  SysprofCallgraphPagePrivate *priv = sysprof_callgraph_page_get_instance_private (self);
  GtkTreeIter iter;
  GtkListStore *callers_store;
  g_autoptr(GHashTable) callers = NULL;
  g_autoptr(GHashTable) processed = NULL;
  StackNode *callees = NULL;
  StackNode *node;
  GObject *selected_item;

  g_assert (SYSPROF_IS_CALLGRAPH_PAGE (self));
  g_assert (GTK_IS_SINGLE_SELECTION (selection));

  selected_item = gtk_single_selection_get_selected_item (GTK_SINGLE_SELECTION (selection));
  if (!selected_item)
    {
      gtk_tree_view_set_model (priv->callers_view, NULL);
      gtk_tree_view_set_model (priv->descendants_view, NULL);
      return;
    }

  g_assert (SYSPROF_IS_STACK_NODE_ITEM (selected_item));
  callees = sysprof_stack_node_item_get_node (SYSPROF_STACK_NODE_ITEM (selected_item));

  sysprof_callgraph_page_update_descendants (self, callees);

  callers_store = gtk_list_store_new (4,
                                      G_TYPE_STRING,
                                      G_TYPE_DOUBLE,
                                      G_TYPE_DOUBLE,
                                      G_TYPE_POINTER);

  callers = g_hash_table_new_full (NULL, NULL, NULL, caller_free);
  processed = g_hash_table_new (NULL, NULL);

  for (node = callees; node != NULL; node = node->next)
    {
      Caller *c;

      if (!node->parent)
        continue;

      c = g_hash_table_lookup (callers, U64_TO_POINTER (node->parent->data));

      if (c == NULL)
        {
          c = caller_new (node->parent);
          g_hash_table_insert (callers, (gpointer)c->name, c);
        }
    }

  for (node = callees; node != NULL; node = node->next)
    {
      StackNode *top_caller = node->parent;
      StackNode *top_callee = node;
      StackNode *n;
      Caller *c;

      if (!node->parent)
        continue;

      /*
       * We could have a situation where the function was called in a
       * reentrant fashion, so we want to take the top-most match in the
       * stack.
       */
      for (n = node; n && n->parent; n = n->parent)
        {
          if (n->data == node->data && n->parent->data == node->parent->data)
            {
              top_caller = n->parent;
              top_callee = n;
            }
        }

      c = g_hash_table_lookup (callers, U64_TO_POINTER (node->parent->data));

      g_assert (c != NULL);

      if (!g_hash_table_lookup (processed, top_caller))
        {
          c->total += top_callee->total;
          g_hash_table_insert (processed, top_caller, top_caller);
        }

      c->self += node->size;
    }

  {
    GHashTableIter hiter;
    gpointer key, value;
    guint size = 0;

    size = MAX (1, sysprof_callgraph_page_get_profile_size (self));

    g_hash_table_iter_init (&hiter, callers);

    while (g_hash_table_iter_next (&hiter, &key, &value))
      {
        Caller *c = value;

        gtk_list_store_append (callers_store, &iter);
        gtk_list_store_set (callers_store, &iter,
                            COLUMN_NAME, c->name,
                            COLUMN_SELF, c->self * 100.0 / size,
                            COLUMN_TOTAL, c->total * 100.0 / size,
                            COLUMN_POINTER, c->node,
                            -1);
      }
  }

  gtk_tree_view_set_model (priv->callers_view, GTK_TREE_MODEL (callers_store));
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (callers_store),
                                        COLUMN_TOTAL,
                                        GTK_SORT_DESCENDING);

  g_clear_object (&callers_store);
}

static void
sysprof_callgraph_page_set_node (SysprofCallgraphPage *self,
                                 StackNode            *node)
{
  SysprofCallgraphPagePrivate *priv = sysprof_callgraph_page_get_instance_private (self);
  GListModel *model;
  uint i;

  g_assert (SYSPROF_IS_CALLGRAPH_PAGE (self));
  g_assert (node != NULL);

  if (priv->profile == NULL)
    return;

  model = G_LIST_MODEL (priv->functions_selection_model);
  for (i = 0; i < g_list_model_get_n_items (model); i++)
    {
      SysprofStackNodeItem *sysprof_item = g_list_model_get_item (model, i);
      StackNode *item = sysprof_stack_node_item_get_node (sysprof_item);
      if (item != NULL && item->data == node->data)
        {
          gtk_single_selection_set_selected (priv->functions_selection_model, i);
          break;
        }
    }
}

static void
sysprof_callgraph_page_descendant_activated (SysprofCallgraphPage *self,
                                             GtkTreePath          *path,
                                             GtkTreeViewColumn    *column,
                                             GtkTreeView          *tree_view)
{
  GtkTreeModel *model;
  StackNode *node = NULL;
  GtkTreeIter iter;

  g_assert (SYSPROF_IS_CALLGRAPH_PAGE (self));
  g_assert (GTK_IS_TREE_VIEW (tree_view));
  g_assert (path != NULL);
  g_assert (GTK_IS_TREE_VIEW_COLUMN (column));

  model = gtk_tree_view_get_model (tree_view);

  if (!gtk_tree_model_get_iter (model, &iter, path))
    return;

  gtk_tree_model_get (model, &iter,
                      COLUMN_POINTER, &node,
                      -1);

  if (node != NULL)
    sysprof_callgraph_page_set_node (self, node);
}

static void
sysprof_callgraph_page_caller_activated (SysprofCallgraphPage   *self,
                                    GtkTreePath       *path,
                                    GtkTreeViewColumn *column,
                                    GtkTreeView       *tree_view)
{
  GtkTreeModel *model;
  StackNode *node = NULL;
  GtkTreeIter iter;

  g_assert (SYSPROF_IS_CALLGRAPH_PAGE (self));
  g_assert (GTK_IS_TREE_VIEW (tree_view));
  g_assert (path != NULL);
  g_assert (GTK_IS_TREE_VIEW_COLUMN (column));

  model = gtk_tree_view_get_model (tree_view);

  if (!gtk_tree_model_get_iter (model, &iter, path))
    return;

  gtk_tree_model_get (model, &iter,
                      COLUMN_POINTER, &node,
                      -1);

  if (node != NULL)
    sysprof_callgraph_page_set_node (self, node);
}

static void
sysprof_callgraph_page_tag_data_func (GtkTreeViewColumn *column,
                                      GtkCellRenderer   *cell,
                                      GtkTreeModel      *model,
                                      GtkTreeIter       *iter,
                                      gpointer           data)
{
  SysprofCallgraphPage *self = data;
  SysprofCallgraphPagePrivate *priv = sysprof_callgraph_page_get_instance_private (self);
  StackNode *node = NULL;
  const gchar *str = NULL;

  if (priv->profile == NULL)
    return;

  gtk_tree_model_get (model, iter, COLUMN_POINTER, &node, -1);

  if (node && node->data)
    {
      GQuark tag;

      tag = sysprof_callgraph_profile_get_tag (priv->profile, GSIZE_TO_POINTER (node->data));
      if (tag != 0)
        str = g_quark_to_string (tag);
    }

  g_object_set (cell, "text", str, NULL);
}

static void
sysprof_callgraph_page_real_go_previous (SysprofCallgraphPage *self)
{
  SysprofCallgraphPagePrivate *priv = sysprof_callgraph_page_get_instance_private (self);
  StackNode *node;

  g_assert (SYSPROF_IS_CALLGRAPH_PAGE (self));

  node = g_queue_pop_head (priv->history);

  if (NULL != (node = g_queue_peek_head (priv->history)))
    sysprof_callgraph_page_set_node (self, node);
}

static gboolean
descendants_view_move_cursor_cb (GtkTreeView     *descendants_view,
                                 GtkMovementStep  step,
                                 int              direction,
                                 gboolean         extend,
                                 gboolean         modify,
                                 gpointer         user_data)
{
  if (step == GTK_MOVEMENT_VISUAL_POSITIONS)
    {
      GtkTreePath *path;

      gtk_tree_view_get_cursor (descendants_view, &path, NULL);

      if (direction == 1)
        {
          gtk_tree_view_expand_row (descendants_view, path, FALSE);
          g_signal_stop_emission_by_name (descendants_view, "move-cursor");
          return FALSE;
        }
      else if (direction == -1)
        {
          gtk_tree_view_collapse_row (descendants_view, path);
          g_signal_stop_emission_by_name (descendants_view, "move-cursor");
          return FALSE;
        }

      gtk_tree_path_free (path);
    }

  return TRUE;
}

static void
copy_tree_view_selection_cb (GtkTreeModel *model,
                             GtkTreePath  *path,
                             GtkTreeIter  *iter,
                             gpointer      data)
{
  g_autofree gchar *name = NULL;
  gchar sstr[16];
  gchar tstr[16];
  GString *str = data;
  gdouble self;
  gdouble total;
  gint depth;

  g_assert (GTK_IS_TREE_MODEL (model));
  g_assert (path != NULL);
  g_assert (iter != NULL);
  g_assert (str != NULL);

  depth = gtk_tree_path_get_depth (path);
  gtk_tree_model_get (model, iter,
                      COLUMN_NAME, &name,
                      COLUMN_SELF, &self,
                      COLUMN_TOTAL, &total,
                      -1);

  g_snprintf (sstr, sizeof sstr, "%.2lf%%", self);
  g_snprintf (tstr, sizeof tstr, "%.2lf%%", total);

  g_string_append_printf (str, "[%8s] [%8s]    ", sstr, tstr);

  for (gint i = 1; i < depth; i++)
    g_string_append (str, "  ");
  g_string_append (str, name);
  g_string_append_c (str, '\n');
}

static void
copy_tree_view_selection (GtkTreeView *tree_view)
{
  g_autoptr(GString) str = NULL;
  GdkClipboard *clipboard;

  g_assert (GTK_IS_TREE_VIEW (tree_view));

  str = g_string_new ("      SELF      TOTAL    FUNCTION\n");
  gtk_tree_selection_selected_foreach (gtk_tree_view_get_selection (tree_view),
                                       copy_tree_view_selection_cb,
                                       str);

  clipboard = gtk_widget_get_clipboard (GTK_WIDGET (tree_view));
  gdk_clipboard_set_text (clipboard, str->str);
}

static void
sysprof_callgraph_page_copy_cb (GtkWidget  *widget,
                                const char *action_name,
                                GVariant   *param)
{
  SysprofCallgraphPage *self = (SysprofCallgraphPage *)widget;
  SysprofCallgraphPagePrivate *priv = sysprof_callgraph_page_get_instance_private (self);
  GtkRoot *toplevel;
  GtkWidget *focus;

  g_assert (GTK_IS_WIDGET (widget));
  g_assert (SYSPROF_IS_CALLGRAPH_PAGE (self));

  if (!(toplevel = gtk_widget_get_root (widget)) ||
      !GTK_IS_ROOT (toplevel) ||
      !(focus = gtk_root_get_focus (toplevel)))
    return;

  if (focus == GTK_WIDGET (priv->descendants_view))
    copy_tree_view_selection (priv->descendants_view);
  else if (focus == GTK_WIDGET (priv->callers_view))
    copy_tree_view_selection (priv->callers_view);
  /*else if (focus == GTK_WIDGET (priv->functions_view))
    copy_tree_view_selection (priv->functions_view);*/
}

static void
sysprof_callgraph_page_generate_cb (GObject      *object,
                                    GAsyncResult *result,
                                    gpointer      user_data)
{
  SysprofProfile *profile = (SysprofProfile *)object;
  SysprofCallgraphPage *self;
  g_autoptr(GTask) task = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (SYSPROF_IS_PROFILE (profile));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);

  if (!sysprof_profile_generate_finish (profile, result, &error))
    g_task_return_error (task, g_steal_pointer (&error));
  else
    sysprof_callgraph_page_set_profile (self, SYSPROF_CALLGRAPH_PROFILE (profile));
}

static void
sysprof_callgraph_page_load_async (SysprofPage             *page,
                                   SysprofCaptureReader    *reader,
                                   SysprofSelection        *selection,
                                   SysprofCaptureCondition *filter,
                                   GCancellable            *cancellable,
                                   GAsyncReadyCallback      callback,
                                   gpointer                 user_data)
{
  SysprofCallgraphPage *self = (SysprofCallgraphPage *)page;
  g_autoptr(SysprofCaptureReader) copy = NULL;
  g_autoptr(SysprofProfile) profile = NULL;
  g_autoptr(GTask) task = NULL;

  g_assert (SYSPROF_IS_CALLGRAPH_PAGE (self));
  g_assert (reader != NULL);
  g_assert (SYSPROF_IS_SELECTION (selection));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_callgraph_page_load_async);

  copy = sysprof_capture_reader_copy (reader);

  profile = sysprof_callgraph_profile_new_with_selection (selection);
  sysprof_profile_set_reader (profile, reader);
  sysprof_profile_generate (profile,
                            cancellable,
                            sysprof_callgraph_page_generate_cb,
                            g_steal_pointer (&task));
}

static gboolean
sysprof_callgraph_page_load_finish (SysprofPage   *page,
                                    GAsyncResult  *result,
                                    GError       **error)
{
  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH_PAGE (page), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
sysprof_callgraph_page_finalize (GObject *object)
{
  SysprofCallgraphPage *self = (SysprofCallgraphPage *)object;
  SysprofCallgraphPagePrivate *priv = sysprof_callgraph_page_get_instance_private (self);

  g_clear_pointer (&priv->history, g_queue_free);
  g_clear_object (&priv->profile);

  G_OBJECT_CLASS (sysprof_callgraph_page_parent_class)->finalize (object);
}

static void
sysprof_callgraph_page_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  SysprofCallgraphPage *self = SYSPROF_CALLGRAPH_PAGE (object);
  SysprofCallgraphPagePrivate *priv = sysprof_callgraph_page_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_PROFILE:
      g_value_set_object (value, priv->profile);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_callgraph_page_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  SysprofCallgraphPage *self = SYSPROF_CALLGRAPH_PAGE (object);

  switch (prop_id)
    {
    case PROP_PROFILE:
      sysprof_callgraph_page_set_profile (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_callgraph_page_class_init (SysprofCallgraphPageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  SysprofPageClass *page_class = SYSPROF_PAGE_CLASS (klass);

  object_class->finalize = sysprof_callgraph_page_finalize;
  object_class->get_property = sysprof_callgraph_page_get_property;
  object_class->set_property = sysprof_callgraph_page_set_property;

  page_class->load_async = sysprof_callgraph_page_load_async;
  page_class->load_finish = sysprof_callgraph_page_load_finish;

  klass->go_previous = sysprof_callgraph_page_real_go_previous;

  properties [PROP_PROFILE] =
    g_param_spec_object ("profile",
                         "Profile",
                         "The callgraph profile to view",
                         SYSPROF_TYPE_CALLGRAPH_PROFILE,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  signals [GO_PREVIOUS] =
    g_signal_new ("go-previous",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (SysprofCallgraphPageClass, go_previous),
                  NULL, NULL, NULL, G_TYPE_NONE, 0);

  g_type_ensure (EGG_TYPE_PANED);
  g_type_ensure (SYSPROF_TYPE_CELL_RENDERER_PERCENT);
  g_type_ensure (SYSPROF_TYPE_STACK_NODE_ITEM);
  g_type_ensure (SYSPROF_TYPE_PERCENT_LABEL);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/sysprof/ui/sysprof-callgraph-page.ui");

  gtk_widget_class_bind_template_child_private (widget_class, SysprofCallgraphPage, callers_view);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofCallgraphPage, functions_view);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofCallgraphPage, functions_selection_model);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofCallgraphPage, functions_sort_model);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofCallgraphPage, function_total_column);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofCallgraphPage, descendants_view);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofCallgraphPage, descendants_name_column);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofCallgraphPage, stack);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofCallgraphPage, callgraph);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofCallgraphPage, empty_state);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofCallgraphPage, loading_state);

  gtk_widget_class_install_action (widget_class, "page.copy", NULL, sysprof_callgraph_page_copy_cb);

  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_c, GDK_CONTROL_MASK, "page.copy", NULL);
  gtk_widget_class_add_binding_signal (widget_class, GDK_KEY_Left, GDK_ALT_MASK, "go-previous", NULL);
}

static void
sysprof_callgraph_page_init (SysprofCallgraphPage *self)
{
  SysprofCallgraphPagePrivate *priv = sysprof_callgraph_page_get_instance_private (self);
  GtkCellRenderer *cell;

  priv->history = g_queue_new ();

  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_stack_set_visible_child (priv->stack, priv->loading_state);

  g_signal_connect_object (priv->functions_selection_model,
                           "selection-changed",
                           G_CALLBACK (sysprof_callgraph_page_function_selection_changed),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (priv->descendants_view,
                           "row-activated",
                           G_CALLBACK (sysprof_callgraph_page_descendant_activated),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (priv->callers_view,
                           "row-activated",
                           G_CALLBACK (sysprof_callgraph_page_caller_activated),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect (priv->descendants_view,
                    "move-cursor",
                    G_CALLBACK (descendants_view_move_cursor_cb),
                    NULL);

  cell = g_object_new (GTK_TYPE_CELL_RENDERER_TEXT,
                       "ellipsize", PANGO_ELLIPSIZE_MIDDLE,
                       "xalign", 0.0f,
                       NULL);
  gtk_tree_view_column_pack_start (priv->descendants_name_column, cell, TRUE);
  gtk_tree_view_column_add_attribute (priv->descendants_name_column, cell, "text", 0);

  cell = g_object_new (GTK_TYPE_CELL_RENDERER_TEXT,
                       "foreground", "#666666",
                       "scale", PANGO_SCALE_SMALL,
                       "xalign", 1.0f,
                       NULL);
  gtk_tree_view_column_pack_start (priv->descendants_name_column, cell, FALSE);
  gtk_tree_view_column_set_cell_data_func (priv->descendants_name_column, cell,
                                           sysprof_callgraph_page_tag_data_func,
                                           self, NULL);

  gtk_tree_selection_set_mode (gtk_tree_view_get_selection (priv->descendants_view),
                               GTK_SELECTION_MULTIPLE);
}

typedef struct _Descendant Descendant;

struct _Descendant
{
  const gchar *name;
  guint        self;
  guint        cumulative;
  Descendant  *parent;
  Descendant  *siblings;
  Descendant  *children;
};

static void
build_tree_cb (StackLink *trace,
               gint       size,
               gpointer   user_data)
{
  Descendant **tree = user_data;
  Descendant *parent = NULL;
  StackLink *link;

  g_assert (trace != NULL);
  g_assert (tree != NULL);

  /* Get last item */
  link = trace;
  while (link->next)
    link = link->next;

  for (; link != NULL; link = link->prev)
    {
      const gchar *address = U64_TO_POINTER (link->data);
      Descendant *prev = NULL;
      Descendant *match = NULL;

      for (match = *tree; match != NULL; match = match->siblings)
        {
          if (match->name == address)
            {
              if (prev != NULL)
                {
                  /* Move to front */
                  prev->siblings = match->siblings;
                  match->siblings = *tree;
                  *tree = match;
                }
              break;
            }
        }

      if (match == NULL)
        {
          /* Have we seen this object further up the tree? */
          for (match = parent; match != NULL; match = match->parent)
            {
              if (match->name == address)
                break;
            }
        }

      if (match == NULL)
        {
          match = g_slice_new (Descendant);
          match->name = address;
          match->cumulative = 0;
          match->self = 0;
          match->children = NULL;
          match->parent = parent;
          match->siblings = *tree;
          *tree = match;
        }

      tree = &match->children;
      parent = match;
    }

  parent->self += size;

  for (; parent != NULL; parent = parent->parent)
    parent->cumulative += size;
}

static Descendant *
build_tree (StackNode *node)
{
  Descendant *tree = NULL;

  for (; node != NULL; node = node->next)
    {
      if (node->toplevel)
        stack_node_foreach_trace (node, build_tree_cb, &tree);
    }

  return tree;
}

static void
append_to_tree_and_free (SysprofCallgraphPage *self,
                         StackStash           *stash,
                         GtkTreeStore         *store,
                         Descendant           *item,
                         GtkTreeIter          *parent)
{
  StackNode *node = NULL;
  GtkTreeIter iter;
  guint profile_size;

  g_assert (GTK_IS_TREE_STORE (store));
  g_assert (item != NULL);

  profile_size = MAX (1, sysprof_callgraph_page_get_profile_size (self));

  gtk_tree_store_append (store, &iter, parent);

  node = stack_stash_find_node (stash, (gpointer)item->name);

  gtk_tree_store_set (store, &iter,
                      COLUMN_NAME, item->name,
                      COLUMN_SELF, item->self * 100.0 / (gdouble)profile_size,
                      COLUMN_TOTAL, item->cumulative * 100.0 / (gdouble)profile_size,
                      COLUMN_POINTER, node,
                      COLUMN_HITS, (guint)item->cumulative,
                      -1);

  if (item->siblings != NULL)
    append_to_tree_and_free (self, stash, store, item->siblings, parent);

  if (item->children != NULL)
    append_to_tree_and_free (self, stash, store, item->children, &iter);

  g_slice_free (Descendant, item);
}

static void
sysprof_callgraph_page_update_descendants (SysprofCallgraphPage *self,
                                           StackNode            *node)
{
  SysprofCallgraphPagePrivate *priv = sysprof_callgraph_page_get_instance_private (self);
  GtkTreeStore *store;

  g_assert (SYSPROF_IS_CALLGRAPH_PAGE (self));

  if (g_queue_peek_head (priv->history) != node)
    g_queue_push_head (priv->history, node);

  store = gtk_tree_store_new (5,
                              G_TYPE_STRING,
                              G_TYPE_DOUBLE,
                              G_TYPE_DOUBLE,
                              G_TYPE_POINTER,
                              G_TYPE_UINT);

  if (priv->profile != NULL)
  {
    StackStash *stash;

    stash = sysprof_callgraph_profile_get_stash (priv->profile);
    if (stash != NULL)
      {
        Descendant *tree;

        tree = build_tree (node);
        if (tree != NULL)
          append_to_tree_and_free (self, stash, store, tree, NULL);
      }
  }

  gtk_tree_view_set_model (priv->descendants_view, GTK_TREE_MODEL (store));
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store),
                                        COLUMN_TOTAL, GTK_SORT_DESCENDING);
  sysprof_callgraph_page_expand_descendants (self);

  g_clear_object (&store);
}

/**
 * sysprof_callgraph_page_screenshot:
 * @self: A #SysprofCallgraphPage.
 *
 * This function will generate a text representation of the descendants tree.
 * This is useful if you want to include various profiling information in a
 * commit message or email.
 *
 * The text generated will match the current row expansion in the tree view.
 *
 * Returns: (nullable) (transfer full): A newly allocated string that should be freed
 *   with g_free().
 */
gchar *
sysprof_callgraph_page_screenshot (SysprofCallgraphPage *self)
{
  SysprofCallgraphPagePrivate *priv = sysprof_callgraph_page_get_instance_private (self);
  GtkTreeView *tree_view;
  GtkTreeModel *model;
  GtkTreePath *tree_path;
  GString *str;
  GtkTreeIter iter;

  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH_PAGE (self), NULL);

  tree_view = priv->descendants_view;

  if (NULL == (model = gtk_tree_view_get_model (tree_view)))
    return NULL;

  /*
   * To avoid having to precalculate the deepest visible row, we
   * put the timing information at the beginning of the line.
   */

  str = g_string_new ("      SELF CUMULATIVE    FUNCTION\n");
  tree_path = gtk_tree_path_new_first ();

  for (;;)
    {
      if (gtk_tree_model_get_iter (model, &iter, tree_path))
        {
          guint depth = gtk_tree_path_get_depth (tree_path);
          StackNode *node;
          gdouble in_self;
          gdouble total;
          guint i;

          gtk_tree_model_get (model, &iter,
                              COLUMN_SELF, &in_self,
                              COLUMN_TOTAL, &total,
                              COLUMN_POINTER, &node,
                              -1);

          g_string_append_printf (str, "[% 7.2lf%%] [% 7.2lf%%]  ", in_self, total);

          for (i = 0; i < depth; i++)
            g_string_append (str, "  ");
          g_string_append (str, GSIZE_TO_POINTER (node->data));
          g_string_append_c (str, '\n');

          if (gtk_tree_view_row_expanded (tree_view, tree_path))
            gtk_tree_path_down (tree_path);
          else
            gtk_tree_path_next (tree_path);

          continue;
        }

      if (!gtk_tree_path_up (tree_path) || !gtk_tree_path_get_depth (tree_path))
        break;

      gtk_tree_path_next (tree_path);
    }

  gtk_tree_path_free (tree_path);

  return g_string_free (str, FALSE);
}

guint
sysprof_callgraph_page_get_n_functions (SysprofCallgraphPage *self)
{
  SysprofCallgraphPagePrivate *priv = sysprof_callgraph_page_get_instance_private (self);
  GtkSelectionModel *model;
  guint ret = 0;

  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH_PAGE (self), 0);

  if (NULL != (model = gtk_column_view_get_model (priv->functions_view)))
    ret = g_list_model_get_n_items (G_LIST_MODEL (model));

  return ret;
}

void
_sysprof_callgraph_page_set_loading (SysprofCallgraphPage *self,
                                     gboolean              loading)
{
  SysprofCallgraphPagePrivate *priv = sysprof_callgraph_page_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_CALLGRAPH_PAGE (self));

  if (loading)
    priv->loading++;
  else
    priv->loading--;

  if (priv->loading)
    gtk_stack_set_visible_child (priv->stack, priv->loading_state);
  else
    gtk_stack_set_visible_child (priv->stack, priv->callgraph);
}

