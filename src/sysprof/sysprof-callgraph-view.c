/* sysprof-callgraph-view.c
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

#include <libpanel.h>

#include "sysprof-resources.h"

#include "sysprof-callgraph-view-private.h"
#include "sysprof-category-icon.h"
#include "sysprof-symbol-label-private.h"
#include "sysprof-tree-expander.h"

enum {
  PROP_0,
  PROP_BOTTOM_UP,
  PROP_CATEGORIZE_FRAMES,
  PROP_CALLGRAPH,
  PROP_DOCUMENT,
  PROP_HIDE_SYSTEM_LIBRARIES,
  PROP_IGNORE_KERNEL_PROCESSES,
  PROP_IGNORE_PROCESS_0,
  PROP_INCLUDE_THREADS,
  PROP_LEFT_HEAVY,
  PROP_MERGE_SIMILAR_PROCESSES,
  PROP_TRACEABLES,
  PROP_UTILITY_SUMMARY,
  PROP_UTILITY_TRACEABLES,
  N_PROPS
};

static void        buildable_iface_init                     (GtkBuildableIface    *iface);
static GListModel *sysprof_callgraph_view_create_model_func (gpointer              item,
                                                             gpointer              user_data);
static void        descendants_selection_changed_cb         (SysprofCallgraphView *self,
                                                             guint                 position,
                                                             guint                 n_items,
                                                             GtkSingleSelection   *single);
static void        sysprof_callgraph_view_queue_reload      (SysprofCallgraphView *self);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (SysprofCallgraphView, sysprof_callgraph_view, GTK_TYPE_WIDGET,
                                  G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, buildable_iface_init))

static GParamSpec *properties [N_PROPS];

static void
sysprof_callgraph_view_set_utility_traceables (SysprofCallgraphView *self,
                                               GListModel           *model)
{
  g_assert (SYSPROF_IS_CALLGRAPH_VIEW (self));
  g_assert (!model || G_IS_LIST_MODEL (model));

  if (g_set_object (&self->utility_traceables, model))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_UTILITY_TRACEABLES]);
}

static void
sysprof_callgraph_view_set_utility_summary (SysprofCallgraphView *self,
                                            GListModel           *model)
{
  g_assert (SYSPROF_IS_CALLGRAPH_VIEW (self));
  g_assert (!model || G_IS_LIST_MODEL (model));

  if (g_set_object (&self->utility_summary, model))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_UTILITY_SUMMARY]);
}

static void
sysprof_callgraph_view_set_descendants (SysprofCallgraphView *self,
                                        GListModel           *model)
{
  g_autoptr(GtkTreeListRowSorter) descendants_sorter = NULL;
  g_autoptr(GtkSingleSelection) descendants_selection = NULL;
  g_autoptr(GtkSortListModel) descendants_sort_model = NULL;
  g_autoptr(GtkTreeListModel) descendants_tree = NULL;
  g_autoptr(GtkTreeListRow) descendants_first = NULL;
  GtkSorter *column_sorter;

  g_assert (SYSPROF_IS_CALLGRAPH_VIEW (self));
  g_assert (G_IS_LIST_MODEL (model));

  gtk_root_set_focus (gtk_widget_get_root (GTK_WIDGET (self)), NULL);

  column_sorter = gtk_column_view_get_sorter (self->descendants_column_view);
  descendants_tree = gtk_tree_list_model_new (g_object_ref (model),
                                              FALSE, FALSE,
                                              sysprof_callgraph_view_create_model_func,
                                              NULL, NULL);
  descendants_sorter = gtk_tree_list_row_sorter_new (g_object_ref (column_sorter));
  descendants_sort_model = gtk_sort_list_model_new (g_object_ref (G_LIST_MODEL (descendants_tree)),
                                                    g_object_ref (GTK_SORTER (descendants_sorter)));
  gtk_sort_list_model_set_incremental (descendants_sort_model, TRUE);
  descendants_selection = gtk_single_selection_new (g_object_ref (G_LIST_MODEL (descendants_sort_model)));
  gtk_single_selection_set_autoselect (descendants_selection, FALSE);
  g_signal_connect_object (descendants_selection,
                           "selection-changed",
                           G_CALLBACK (descendants_selection_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);
  gtk_column_view_set_model (self->descendants_column_view,
                             GTK_SELECTION_MODEL (descendants_selection));

  if ((descendants_first = gtk_tree_list_model_get_row (descendants_tree, 0)))
    gtk_tree_list_row_set_expanded (descendants_first, TRUE);

}

static void
sysprof_callgraph_view_descendants_cb (GObject      *object,
                                       GAsyncResult *result,
                                       gpointer      user_data)
{
  SysprofCallgraph *callgraph = (SysprofCallgraph *)object;
  g_autoptr(SysprofCallgraphView) self = user_data;
  g_autoptr(GListModel) model = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (SYSPROF_IS_CALLGRAPH (callgraph));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (SYSPROF_IS_CALLGRAPH_VIEW (self));

  if ((model = sysprof_callgraph_descendants_finish (callgraph, result, &error)))
    sysprof_callgraph_view_set_descendants (self, model);
}

static void
callers_selection_changed_cb (SysprofCallgraphView *self,
                              guint                 position,
                              guint                 n_items,
                              GtkSingleSelection   *single)
{
  GObject *object;

  g_assert (SYSPROF_IS_CALLGRAPH_VIEW (self));
  g_assert (GTK_IS_SINGLE_SELECTION (single));

  if ((object = gtk_single_selection_get_selected_item (single)))
    {
      SysprofCallgraphSymbol *sym = SYSPROF_CALLGRAPH_SYMBOL (object);
      SysprofSymbol *symbol = sysprof_callgraph_symbol_get_symbol (sym);

      g_debug ("Select %s as root callgraph node",
               sysprof_symbol_get_name (symbol));

      sysprof_callgraph_descendants_async (self->callgraph,
                                           symbol,
                                           NULL,
                                           sysprof_callgraph_view_descendants_cb,
                                           g_object_ref (self));
    }
}

static void
sysprof_callgraph_view_list_traceables_cb (GObject      *object,
                                           GAsyncResult *result,
                                           gpointer      user_data)
{
  SysprofCallgraphFrame *frame = (SysprofCallgraphFrame *)object;
  g_autoptr(SysprofCallgraphView) self = user_data;
  g_autoptr(GListModel) model = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (SYSPROF_IS_CALLGRAPH_FRAME (frame));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (SYSPROF_IS_CALLGRAPH_VIEW (self));

  model = sysprof_callgraph_frame_list_traceables_finish (frame, result, &error);

  sysprof_callgraph_view_set_utility_traceables (self, model);
}

static void
sysprof_callgraph_view_summarize_cb (GObject      *object,
                                     GAsyncResult *result,
                                     gpointer      user_data)
{
  SysprofCallgraphFrame *frame = (SysprofCallgraphFrame *)object;
  g_autoptr(SysprofCallgraphView) self = user_data;
  g_autoptr(GListModel) model = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (SYSPROF_IS_CALLGRAPH_FRAME (frame));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (SYSPROF_IS_CALLGRAPH_VIEW (self));

  model = sysprof_callgraph_frame_summarize_finish (frame, result, &error);

  sysprof_callgraph_view_set_utility_summary (self, model);
}

static void
descendants_selection_changed_cb (SysprofCallgraphView *self,
                                  guint                 position,
                                  guint                 n_items,
                                  GtkSingleSelection   *single)
{
  g_autoptr(GObject) item = NULL;
  GObject *object;

  g_assert (SYSPROF_IS_CALLGRAPH_VIEW (self));
  g_assert (GTK_IS_SINGLE_SELECTION (single));

  sysprof_callgraph_view_set_utility_summary (self, NULL);
  sysprof_callgraph_view_set_utility_traceables (self, NULL);

  if ((object = gtk_single_selection_get_selected_item (single)) &&
      GTK_IS_TREE_LIST_ROW (object) &&
      (item = gtk_tree_list_row_get_item (GTK_TREE_LIST_ROW (object))) &&
      SYSPROF_IS_CALLGRAPH_FRAME (item))
    {
      sysprof_callgraph_frame_summarize_async (SYSPROF_CALLGRAPH_FRAME (item),
                                               NULL,
                                               sysprof_callgraph_view_summarize_cb,
                                               g_object_ref (self));
      sysprof_callgraph_frame_list_traceables_async (SYSPROF_CALLGRAPH_FRAME (item),
                                                     NULL,
                                                     sysprof_callgraph_view_list_traceables_cb,
                                                     g_object_ref (self));
    }
}

static void
functions_selection_changed_cb (SysprofCallgraphView *self,
                                guint                 position,
                                guint                 n_items,
                                GtkSingleSelection   *single)
{
  GObject *object;

  g_assert (SYSPROF_IS_CALLGRAPH_VIEW (self));
  g_assert (GTK_IS_SINGLE_SELECTION (single));

  if ((object = gtk_single_selection_get_selected_item (single)))
    {
      SysprofCallgraphSymbol *sym = SYSPROF_CALLGRAPH_SYMBOL (object);
      SysprofSymbol *symbol = sysprof_callgraph_symbol_get_symbol (sym);
      g_autoptr(GtkSortListModel) callers_sort_model = NULL;
      g_autoptr(GtkSingleSelection) callers_selection = NULL;
      g_autoptr(GListModel) callers = sysprof_callgraph_list_callers (self->callgraph, symbol);
      GtkSorter *column_sorter;

      column_sorter = gtk_column_view_get_sorter (self->callers_column_view);
      callers_sort_model = gtk_sort_list_model_new (g_object_ref (callers),
                                                    g_object_ref (column_sorter));
      callers_selection = gtk_single_selection_new (g_object_ref (G_LIST_MODEL (callers_sort_model)));
      gtk_single_selection_set_autoselect (callers_selection, FALSE);
      gtk_single_selection_set_selected (callers_selection, GTK_INVALID_LIST_POSITION);
      g_signal_connect_object (callers_selection,
                               "selection-changed",
                               G_CALLBACK (callers_selection_changed_cb),
                               self,
                               G_CONNECT_SWAPPED);
      gtk_column_view_set_model (self->callers_column_view,
                                 GTK_SELECTION_MODEL (callers_selection));

      switch (sysprof_symbol_get_kind (symbol))
        {
        case SYSPROF_SYMBOL_KIND_ROOT:
          sysprof_callgraph_view_set_descendants (self, G_LIST_MODEL (self->callgraph));
          break;

        default:
        case SYSPROF_SYMBOL_KIND_PROCESS:
        case SYSPROF_SYMBOL_KIND_THREAD:
        case SYSPROF_SYMBOL_KIND_CONTEXT_SWITCH:
        case SYSPROF_SYMBOL_KIND_UNWINDABLE:
        case SYSPROF_SYMBOL_KIND_USER:
        case SYSPROF_SYMBOL_KIND_KERNEL:
          sysprof_callgraph_descendants_async (self->callgraph,
                                               symbol,
                                               NULL,
                                               sysprof_callgraph_view_descendants_cb,
                                               g_object_ref (self));
          break;
        }
    }
  else
    {
      gtk_column_view_set_model (self->callers_column_view, NULL);
    }
}

static void
make_descendant_root_action (GtkWidget  *widget,
                             const char *action_name,
                             GVariant   *param)
{
  SysprofCallgraphView *self = (SysprofCallgraphView *)widget;
  g_autoptr(SysprofCallgraphFrame) frame = NULL;
  GtkSelectionModel *model;
  GtkTreeListRow *row;

  g_assert (SYSPROF_IS_CALLGRAPH_VIEW (self));

  if ((model = gtk_column_view_get_model (self->descendants_column_view)) &&
      GTK_IS_SINGLE_SELECTION (model) &&
      (row = gtk_single_selection_get_selected_item (GTK_SINGLE_SELECTION (model))) &&
      GTK_IS_TREE_LIST_ROW (row) &&
      (frame = gtk_tree_list_row_get_item (row)) &&
      SYSPROF_IS_CALLGRAPH_FRAME (frame))
    {
      SysprofSymbol *symbol = sysprof_callgraph_frame_get_symbol (frame);

      if (sysprof_symbol_get_kind (symbol) != SYSPROF_SYMBOL_KIND_ROOT)
        {
          sysprof_callgraph_view_set_utility_summary (self, NULL);
          sysprof_callgraph_view_set_utility_traceables (self, NULL);
          gtk_column_view_set_model (self->descendants_column_view, NULL);

          sysprof_callgraph_descendants_async (self->callgraph,
                                               symbol,
                                               NULL,
                                               sysprof_callgraph_view_descendants_cb,
                                               g_object_ref (self));
        }
    }
}

static void
sysprof_callgraph_view_dispose (GObject *object)
{
  SysprofCallgraphView *self = (SysprofCallgraphView *)object;

  if (self->callgraph != NULL)
    {
      if (SYSPROF_CALLGRAPH_VIEW_GET_CLASS (self)->unload)
        SYSPROF_CALLGRAPH_VIEW_GET_CLASS (self)->unload (self);

      g_clear_object (&self->callgraph);
    }

  if (self->functions_column_view)
    gtk_column_view_set_model (self->functions_column_view, NULL);

  if (self->callers_column_view)
    gtk_column_view_set_model (self->callers_column_view, NULL);

  if (self->descendants_column_view)
    gtk_column_view_set_model (self->descendants_column_view, NULL);

  if (self->traceables_signals)
    {
      g_signal_group_set_target (self->traceables_signals, NULL);
      g_clear_object (&self->traceables_signals);
    }

  gtk_widget_dispose_template (GTK_WIDGET (self), SYSPROF_TYPE_CALLGRAPH_VIEW);

  g_clear_handle_id (&self->reload_source, g_source_remove);

  g_clear_pointer (&self->paned, gtk_widget_unparent);

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);

  g_clear_object (&self->document);
  g_clear_object (&self->traceables);
  g_clear_object (&self->utility_summary);
  g_clear_object (&self->utility_traceables);

  G_OBJECT_CLASS (sysprof_callgraph_view_parent_class)->dispose (object);
}

static void
sysprof_callgraph_view_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  SysprofCallgraphView *self = SYSPROF_CALLGRAPH_VIEW (object);

  switch (prop_id)
    {
    case PROP_BOTTOM_UP:
      g_value_set_boolean (value, sysprof_callgraph_view_get_bottom_up (self));
      break;

    case PROP_CATEGORIZE_FRAMES:
      g_value_set_boolean (value, sysprof_callgraph_view_get_categorize_frames (self));
      break;

    case PROP_CALLGRAPH:
      g_value_set_object (value, sysprof_callgraph_view_get_callgraph (self));
      break;

    case PROP_DOCUMENT:
      g_value_set_object (value, sysprof_callgraph_view_get_document (self));
      break;

    case PROP_HIDE_SYSTEM_LIBRARIES:
      g_value_set_boolean (value, sysprof_callgraph_view_get_hide_system_libraries (self));
      break;

    case PROP_INCLUDE_THREADS:
      g_value_set_boolean (value, sysprof_callgraph_view_get_include_threads (self));
      break;

    case PROP_LEFT_HEAVY:
      g_value_set_boolean (value, sysprof_callgraph_view_get_left_heavy (self));
      break;

    case PROP_MERGE_SIMILAR_PROCESSES:
      g_value_set_boolean (value, sysprof_callgraph_view_get_merge_similar_processes (self));
      break;

    case PROP_TRACEABLES:
      g_value_set_object (value, sysprof_callgraph_view_get_traceables (self));
      break;

    case PROP_UTILITY_SUMMARY:
      g_value_set_object (value, self->utility_summary);
      break;

    case PROP_UTILITY_TRACEABLES:
      g_value_set_object (value, self->utility_traceables);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_callgraph_view_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  SysprofCallgraphView *self = SYSPROF_CALLGRAPH_VIEW (object);

  switch (prop_id)
    {
    case PROP_BOTTOM_UP:
      sysprof_callgraph_view_set_bottom_up (self, g_value_get_boolean (value));
      break;

    case PROP_CATEGORIZE_FRAMES:
      sysprof_callgraph_view_set_categorize_frames (self, g_value_get_boolean (value));
      break;

    case PROP_DOCUMENT:
      sysprof_callgraph_view_set_document (self, g_value_get_object (value));
      break;

    case PROP_HIDE_SYSTEM_LIBRARIES:
      sysprof_callgraph_view_set_hide_system_libraries (self, g_value_get_boolean (value));
      break;

    case PROP_IGNORE_PROCESS_0:
      sysprof_callgraph_view_set_ignore_process_0 (self, g_value_get_boolean (value));
      break;

    case PROP_IGNORE_KERNEL_PROCESSES:
      sysprof_callgraph_view_set_ignore_kernel_processes (self, g_value_get_boolean (value));
      break;

    case PROP_INCLUDE_THREADS:
      sysprof_callgraph_view_set_include_threads (self, g_value_get_boolean (value));
      break;

    case PROP_LEFT_HEAVY:
      sysprof_callgraph_view_set_left_heavy (self, g_value_get_boolean (value));
      break;

    case PROP_MERGE_SIMILAR_PROCESSES:
      sysprof_callgraph_view_set_merge_similar_processes (self, g_value_get_boolean (value));
      break;

    case PROP_TRACEABLES:
      sysprof_callgraph_view_set_traceables (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_callgraph_view_class_init (SysprofCallgraphViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_callgraph_view_dispose;
  object_class->get_property = sysprof_callgraph_view_get_property;
  object_class->set_property = sysprof_callgraph_view_set_property;

  properties[PROP_BOTTOM_UP] =
    g_param_spec_boolean ("bottom-up", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_CATEGORIZE_FRAMES] =
    g_param_spec_boolean ("categorize-frames", NULL, NULL,
                          TRUE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_CALLGRAPH] =
    g_param_spec_object ("callgraph", NULL, NULL,
                         SYSPROF_TYPE_CALLGRAPH,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_DOCUMENT] =
    g_param_spec_object ("document", NULL, NULL,
                         SYSPROF_TYPE_DOCUMENT,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_HIDE_SYSTEM_LIBRARIES] =
    g_param_spec_boolean ("hide-system-libraries", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_INCLUDE_THREADS] =
    g_param_spec_boolean ("include-threads", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_IGNORE_PROCESS_0] =
    g_param_spec_boolean ("ignore-process-0", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_IGNORE_KERNEL_PROCESSES] =
    g_param_spec_boolean ("ignore-kernel-processes", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_LEFT_HEAVY] =
    g_param_spec_boolean ("left-heavy", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_MERGE_SIMILAR_PROCESSES] =
    g_param_spec_boolean ("merge-similar-processes", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_TRACEABLES] =
    g_param_spec_object ("traceables", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_UTILITY_SUMMARY] =
    g_param_spec_object ("utility-summary", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_UTILITY_TRACEABLES] =
    g_param_spec_object ("utility-traceables", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-callgraph-view.ui");
  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_set_css_name (widget_class, "callgraphview");

  gtk_widget_class_bind_template_child (widget_class, SysprofCallgraphView, callers_column_view);
  gtk_widget_class_bind_template_child (widget_class, SysprofCallgraphView, descendants_column_view);
  gtk_widget_class_bind_template_child (widget_class, SysprofCallgraphView, descendants_name_sorter);
  gtk_widget_class_bind_template_child (widget_class, SysprofCallgraphView, functions_column_view);
  gtk_widget_class_bind_template_child (widget_class, SysprofCallgraphView, functions_name_sorter);
  gtk_widget_class_bind_template_child (widget_class, SysprofCallgraphView, function_filter);
  gtk_widget_class_bind_template_child (widget_class, SysprofCallgraphView, paned);

  gtk_widget_class_install_action (widget_class, "callgraph.make-descendant-root", NULL, make_descendant_root_action);

  klass->augment_size = GLIB_SIZEOF_VOID_P;

  g_resources_register (sysprof_get_resource ());

  g_type_ensure (PANEL_TYPE_PANED);
  g_type_ensure (SYSPROF_TYPE_CALLGRAPH_SYMBOL);
  g_type_ensure (SYSPROF_TYPE_CATEGORY_ICON);
  g_type_ensure (SYSPROF_TYPE_SYMBOL_LABEL);
  g_type_ensure (SYSPROF_TYPE_TREE_EXPANDER);
}

static void
sysprof_callgraph_view_init (SysprofCallgraphView *self)
{
  self->categorize_frames = TRUE;

  self->traceables_signals = g_signal_group_new (G_TYPE_LIST_MODEL);
  g_signal_connect_object (self->traceables_signals,
                           "bind",
                           G_CALLBACK (sysprof_callgraph_view_queue_reload),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_group_connect_object (self->traceables_signals,
                                 "items-changed",
                                 G_CALLBACK (sysprof_callgraph_view_queue_reload),
                                 self,
                                 G_CONNECT_SWAPPED);

  gtk_widget_init_template (GTK_WIDGET (self));
}

static int
descendants_name_compare (gconstpointer a,
                          gconstpointer b,
                          gpointer      user_data)
{
  SysprofCallgraphFrame *frame_a = (SysprofCallgraphFrame *)a;
  SysprofCallgraphFrame *frame_b = (SysprofCallgraphFrame *)b;

  return g_strcmp0 (sysprof_symbol_get_name (sysprof_callgraph_frame_get_symbol (frame_a)),
                    sysprof_symbol_get_name (sysprof_callgraph_frame_get_symbol (frame_b)));
}

static int
functions_name_compare (gconstpointer a,
                        gconstpointer b,
                        gpointer      user_data)
{
  SysprofCallgraphSymbol *sym_a = (SysprofCallgraphSymbol *)a;
  SysprofCallgraphSymbol *sym_b = (SysprofCallgraphSymbol *)b;

  return g_strcmp0 (sysprof_symbol_get_name (sysprof_callgraph_symbol_get_symbol (sym_a)),
                    sysprof_symbol_get_name (sysprof_callgraph_symbol_get_symbol (sym_b)));
}

static GListModel *
sysprof_callgraph_view_create_model_func (gpointer item,
                                          gpointer user_data)
{
  if (g_list_model_get_n_items (G_LIST_MODEL (item)) > 0)
    return g_object_ref (G_LIST_MODEL (item));

  return NULL;
}

static void
sysprof_callgraph_view_reload_cb (GObject      *object,
                                  GAsyncResult *result,
                                  gpointer      user_data)
{
  SysprofDocument *document = (SysprofDocument *)object;
  g_autoptr(SysprofCallgraphView) self = user_data;
  g_autoptr(GtkFilterListModel) filter_model = NULL;
  g_autoptr(SysprofCallgraph) callgraph = NULL;
  g_autoptr(GError) error = NULL;
  GtkSorter *column_sorter;

  g_autoptr(GtkSingleSelection) functions_selection = NULL;
  g_autoptr(GtkSortListModel) functions_sort_model = NULL;
  g_autoptr(GListModel) functions_model = NULL;

  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (SYSPROF_IS_CALLGRAPH_VIEW (self));

  if (!(callgraph = sysprof_document_callgraph_finish (document, result, &error)))
    {
      g_debug ("Failed to generate callgraph: %s", error->message);
      return;
    }

  if (SYSPROF_CALLGRAPH_VIEW_GET_CLASS (self)->unload)
    SYSPROF_CALLGRAPH_VIEW_GET_CLASS (self)->unload (self);

  g_set_object (&self->callgraph, callgraph);

  sysprof_callgraph_view_set_descendants (self, G_LIST_MODEL (callgraph));

  column_sorter = gtk_column_view_get_sorter (self->functions_column_view);
  functions_model = sysprof_callgraph_list_symbols (callgraph);
  filter_model = gtk_filter_list_model_new (g_object_ref (G_LIST_MODEL (functions_model)),
                                            g_object_ref (GTK_FILTER (self->function_filter)));
  gtk_filter_list_model_set_incremental (filter_model, TRUE);
  functions_sort_model = gtk_sort_list_model_new (g_object_ref (G_LIST_MODEL (filter_model)),
                                                  g_object_ref (column_sorter));
  gtk_sort_list_model_set_incremental (functions_sort_model, TRUE);
  functions_selection = gtk_single_selection_new (g_object_ref (G_LIST_MODEL (functions_sort_model)));
  gtk_single_selection_set_autoselect (functions_selection, FALSE);
  gtk_single_selection_set_can_unselect (functions_selection, TRUE);
  gtk_single_selection_set_selected (functions_selection, GTK_INVALID_LIST_POSITION);
  g_signal_connect_object (functions_selection,
                           "selection-changed",
                           G_CALLBACK (functions_selection_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);
  gtk_column_view_set_model (self->functions_column_view,
                             GTK_SELECTION_MODEL (functions_selection));

  gtk_custom_sorter_set_sort_func (self->descendants_name_sorter,
                                   descendants_name_compare, NULL, NULL);
  gtk_custom_sorter_set_sort_func (self->functions_name_sorter,
                                   functions_name_compare, NULL, NULL);

  if (SYSPROF_CALLGRAPH_VIEW_GET_CLASS (self)->load)
    SYSPROF_CALLGRAPH_VIEW_GET_CLASS (self)->load (self, callgraph);

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CALLGRAPH]);
}

static gboolean
sysprof_callgraph_view_reload (SysprofCallgraphView *self)
{
  SysprofCallgraphFlags flags = 0;

  g_assert (SYSPROF_IS_CALLGRAPH_VIEW (self));

  g_clear_handle_id (&self->reload_source, g_source_remove);

  if (self->ignore_process_0)
    flags |= SYSPROF_CALLGRAPH_FLAGS_IGNORE_PROCESS_0;

  if (self->include_threads)
    flags |= SYSPROF_CALLGRAPH_FLAGS_INCLUDE_THREADS;

  if (self->hide_system_libraries)
    flags |= SYSPROF_CALLGRAPH_FLAGS_HIDE_SYSTEM_LIBRARIES;

  if (self->bottom_up)
    flags |= SYSPROF_CALLGRAPH_FLAGS_BOTTOM_UP;

  if (self->categorize_frames)
    flags |= SYSPROF_CALLGRAPH_FLAGS_CATEGORIZE_FRAMES;

  if (self->left_heavy)
    flags |= SYSPROF_CALLGRAPH_FLAGS_LEFT_HEAVY;

  if (self->merge_similar_processes)
    flags |= SYSPROF_CALLGRAPH_FLAGS_MERGE_SIMILAR_PROCESSES;

  if (self->ignore_kernel_processes)
    flags |= SYSPROF_CALLGRAPH_FLAGS_IGNORE_KERNEL_PROCESSES;

  sysprof_document_callgraph_async (self->document,
                                    flags,
                                    self->traceables,
                                    SYSPROF_CALLGRAPH_VIEW_GET_CLASS (self)->augment_size,
                                    SYSPROF_CALLGRAPH_VIEW_GET_CLASS (self)->augment_func,
                                    NULL,
                                    NULL,
                                    self->cancellable,
                                    sysprof_callgraph_view_reload_cb,
                                    g_object_ref (self));

  return G_SOURCE_REMOVE;
}

static void
sysprof_callgraph_view_queue_reload (SysprofCallgraphView *self)
{
  g_assert (SYSPROF_IS_CALLGRAPH_VIEW (self));

  sysprof_callgraph_view_set_utility_traceables (self, NULL);

  gtk_column_view_set_model (self->descendants_column_view, NULL);
  gtk_column_view_set_model (self->functions_column_view, NULL);
  gtk_column_view_set_model (self->callers_column_view, NULL);

  g_clear_handle_id (&self->reload_source, g_source_remove);
  g_cancellable_cancel (self->cancellable);

  g_clear_object (&self->cancellable);
  self->cancellable = g_cancellable_new ();

  if (self->document != NULL && self->traceables != NULL)
    self->reload_source = g_idle_add_full (G_PRIORITY_LOW,
                                           (GSourceFunc)sysprof_callgraph_view_reload,
                                           g_object_ref (self),
                                           g_object_unref);
}

/**
 * sysprof_callgraph_view_get_document:
 * @self: a #SysprofCallgraphView
 *
 * Gets the document for the callgraph.
 *
 * Returns: (transfer none) (nullable): a #SysprofDocument or %NULL
 */
SysprofDocument *
sysprof_callgraph_view_get_document (SysprofCallgraphView *self)
{
  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH_VIEW (self), NULL);

  return self->document;
}

void
sysprof_callgraph_view_set_document (SysprofCallgraphView *self,
                                     SysprofDocument      *document)
{
  g_return_if_fail (SYSPROF_IS_CALLGRAPH_VIEW (self));

  if (g_set_object (&self->document, document))
    {
      sysprof_callgraph_view_queue_reload (self);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DOCUMENT]);
    }
}

/**
 * sysprof_callgraph_view_get_traceables:
 * @self: a #SysprofCallgraphView
 *
 * Gets the list of traceables to be displayed.
 *
 * Returns: (transfer none) (nullable): a #GListModel or %NULL
 */
GListModel *
sysprof_callgraph_view_get_traceables (SysprofCallgraphView *self)
{
  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH_VIEW (self), NULL);

  return self->traceables;
}

void
sysprof_callgraph_view_set_traceables (SysprofCallgraphView *self,
                                       GListModel           *traceables)
{
  g_return_if_fail (SYSPROF_IS_CALLGRAPH_VIEW (self));
  g_return_if_fail (!traceables || G_IS_LIST_MODEL (traceables));

  if (g_set_object (&self->traceables, traceables))
    {
      g_signal_group_set_target (self->traceables_signals, traceables);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TRACEABLES]);
    }
}

static GObject *
sysprof_callgraph_view_get_internal_child (GtkBuildable *buildable,
                                           GtkBuilder   *builder,
                                           const char   *name)
{
  if (g_strcmp0 (name, "callers_column_view") == 0)
    return G_OBJECT (SYSPROF_CALLGRAPH_VIEW (buildable)->callers_column_view);
  else if (g_strcmp0 (name, "descendants_column_view") == 0)
    return G_OBJECT (SYSPROF_CALLGRAPH_VIEW (buildable)->descendants_column_view);
  else if (g_strcmp0 (name, "functions_column_view") == 0)
    return G_OBJECT (SYSPROF_CALLGRAPH_VIEW (buildable)->functions_column_view);

  return NULL;
}

static void
buildable_iface_init (GtkBuildableIface *iface)
{
  iface->get_internal_child = sysprof_callgraph_view_get_internal_child;
}

/**
 * sysprof_callgraph_view_get_callgraph:
 * @self: a #SysprofCallgraphView
 *
 * Gets the callgraph being displayed.
 *
 * Returns: (transfer none) (nullable): a #SysprofCallgraph or %NULL
 */
SysprofCallgraph *
sysprof_callgraph_view_get_callgraph (SysprofCallgraphView *self)
{
  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH_VIEW (self), NULL);

  return self->callgraph;
}

gboolean
sysprof_callgraph_view_get_bottom_up (SysprofCallgraphView *self)
{
  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH_VIEW (self), FALSE);

  return self->bottom_up;
}

void
sysprof_callgraph_view_set_bottom_up (SysprofCallgraphView *self,
                                      gboolean              bottom_up)
{
  g_return_if_fail (SYSPROF_IS_CALLGRAPH_VIEW (self));

  bottom_up = !!bottom_up;

  if (self->bottom_up != bottom_up)
    {
      self->bottom_up = bottom_up;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BOTTOM_UP]);
      sysprof_callgraph_view_queue_reload (self);
    }
}

gboolean
sysprof_callgraph_view_get_categorize_frames (SysprofCallgraphView *self)
{
  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH_VIEW (self), FALSE);

  return self->categorize_frames;
}

void
sysprof_callgraph_view_set_categorize_frames (SysprofCallgraphView *self,
                                              gboolean              categorize_frames)
{
  g_return_if_fail (SYSPROF_IS_CALLGRAPH_VIEW (self));

  categorize_frames = !!categorize_frames;

  if (self->categorize_frames != categorize_frames)
    {
      self->categorize_frames = categorize_frames;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CATEGORIZE_FRAMES]);
      sysprof_callgraph_view_queue_reload (self);
    }
}

gboolean
sysprof_callgraph_view_get_hide_system_libraries (SysprofCallgraphView *self)
{
  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH_VIEW (self), FALSE);

  return self->hide_system_libraries;
}

void
sysprof_callgraph_view_set_hide_system_libraries (SysprofCallgraphView *self,
                                                  gboolean              hide_system_libraries)
{
  g_return_if_fail (SYSPROF_IS_CALLGRAPH_VIEW (self));

  hide_system_libraries = !!hide_system_libraries;

  if (self->hide_system_libraries != hide_system_libraries)
    {
      self->hide_system_libraries = hide_system_libraries;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HIDE_SYSTEM_LIBRARIES]);
      sysprof_callgraph_view_queue_reload (self);
    }
}

gboolean
sysprof_callgraph_view_get_include_threads (SysprofCallgraphView *self)
{
  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH_VIEW (self), FALSE);

  return self->include_threads;
}

void
sysprof_callgraph_view_set_include_threads (SysprofCallgraphView *self,
                                            gboolean              include_threads)
{
  g_return_if_fail (SYSPROF_IS_CALLGRAPH_VIEW (self));

  include_threads = !!include_threads;

  if (self->include_threads != include_threads)
    {
      self->include_threads = include_threads;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INCLUDE_THREADS]);
      sysprof_callgraph_view_queue_reload (self);
    }
}

gboolean
sysprof_callgraph_view_get_ignore_kernel_processes (SysprofCallgraphView *self)
{
  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH_VIEW (self), FALSE);

  return self->ignore_kernel_processes;
}

void
sysprof_callgraph_view_set_ignore_kernel_processes (SysprofCallgraphView *self,
                                                    gboolean              ignore_kernel_processes)
{
  g_return_if_fail (SYSPROF_IS_CALLGRAPH_VIEW (self));

  ignore_kernel_processes = !!ignore_kernel_processes;

  if (self->ignore_kernel_processes != ignore_kernel_processes)
    {
      self->ignore_kernel_processes = ignore_kernel_processes;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IGNORE_KERNEL_PROCESSES]);
      sysprof_callgraph_view_queue_reload (self);
    }
}

gboolean
sysprof_callgraph_view_get_ignore_process_0 (SysprofCallgraphView *self)
{
  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH_VIEW (self), FALSE);

  return self->ignore_process_0;
}

void
sysprof_callgraph_view_set_ignore_process_0 (SysprofCallgraphView *self,
                                             gboolean              ignore_process_0)
{
  g_return_if_fail (SYSPROF_IS_CALLGRAPH_VIEW (self));

  ignore_process_0 = !!ignore_process_0;

  if (self->ignore_process_0 != ignore_process_0)
    {
      self->ignore_process_0 = ignore_process_0;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IGNORE_PROCESS_0]);
      sysprof_callgraph_view_queue_reload (self);
    }
}

gboolean
sysprof_callgraph_view_get_left_heavy (SysprofCallgraphView *self)
{
  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH_VIEW (self), FALSE);

  return self->left_heavy;
}

void
sysprof_callgraph_view_set_left_heavy (SysprofCallgraphView *self,
                                       gboolean              left_heavy)
{
  g_return_if_fail (SYSPROF_IS_CALLGRAPH_VIEW (self));

  left_heavy = !!left_heavy;

  if (self->left_heavy != left_heavy)
    {
      self->left_heavy = left_heavy;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LEFT_HEAVY]);
      sysprof_callgraph_view_queue_reload (self);
    }
}

gboolean
sysprof_callgraph_view_get_merge_similar_processes (SysprofCallgraphView *self)
{
  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH_VIEW (self), FALSE);

  return self->merge_similar_processes;
}

void
sysprof_callgraph_view_set_merge_similar_processes (SysprofCallgraphView *self,
                                                    gboolean              merge_similar_processes)
{
  g_return_if_fail (SYSPROF_IS_CALLGRAPH_VIEW (self));

  merge_similar_processes = !!merge_similar_processes;

  if (self->merge_similar_processes != merge_similar_processes)
    {
      self->merge_similar_processes = merge_similar_processes;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MERGE_SIMILAR_PROCESSES]);
      sysprof_callgraph_view_queue_reload (self);
    }
}
