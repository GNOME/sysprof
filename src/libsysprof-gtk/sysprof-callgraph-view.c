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

#include "libsysprof-gtk-resources.h"

#include "sysprof-callgraph-view-private.h"

enum {
  PROP_0,
  PROP_DOCUMENT,
  PROP_TRACEABLES,
  N_PROPS
};

static void buildable_iface_init (GtkBuildableIface *iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (SysprofCallgraphView, sysprof_callgraph_view, GTK_TYPE_WIDGET,
                                  G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, buildable_iface_init))

static GParamSpec *properties [N_PROPS];

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

      g_print ("Caller %s selected.\n",
               sysprof_symbol_get_name (symbol));
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
    }
  else
    {
      gtk_column_view_set_model (self->callers_column_view, NULL);
    }
}

static gboolean
sysprof_callgraph_view_key_pressed_cb (GtkTreeExpander       *expander,
                                       guint                  keyval,
                                       guint                  keycode,
                                       GdkModifierType        state,
                                       GtkEventControllerKey *controller)
{
  GtkTreeListRow *row;

  g_assert (GTK_IS_TREE_EXPANDER (expander));
  g_assert (GTK_IS_EVENT_CONTROLLER_KEY (controller));

  row = gtk_tree_expander_get_list_row (expander);

  if (keyval ==  GDK_KEY_space)
    gtk_tree_list_row_set_expanded (row, !gtk_tree_list_row_get_expanded (row));
  else if (keyval == GDK_KEY_Right)
    gtk_tree_list_row_set_expanded (row, TRUE);
  else if (keyval == GDK_KEY_Left)
    gtk_tree_list_row_set_expanded (row, FALSE);
  else
    return FALSE;

  return TRUE;
}

static void
sysprof_callgraph_view_dispose (GObject *object)
{
  SysprofCallgraphView *self = (SysprofCallgraphView *)object;

  g_clear_handle_id (&self->reload_source, g_source_remove);

  g_clear_pointer (&self->paned, gtk_widget_unparent);

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);

  g_clear_object (&self->callgraph);
  g_clear_object (&self->document);
  g_clear_object (&self->traceables);

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
    case PROP_DOCUMENT:
      g_value_set_object (value, sysprof_callgraph_view_get_document (self));
      break;

    case PROP_TRACEABLES:
      g_value_set_object (value, sysprof_callgraph_view_get_traceables (self));
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
    case PROP_DOCUMENT:
      sysprof_callgraph_view_set_document (self, g_value_get_object (value));
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

  properties[PROP_DOCUMENT] =
    g_param_spec_object ("document", NULL, NULL,
                         SYSPROF_TYPE_DOCUMENT,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_TRACEABLES] =
    g_param_spec_object ("traceables", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/libsysprof-gtk/sysprof-callgraph-view.ui");
  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_bind_template_child (widget_class, SysprofCallgraphView, callers_column_view);
  gtk_widget_class_bind_template_child (widget_class, SysprofCallgraphView, descendants_column_view);
  gtk_widget_class_bind_template_child (widget_class, SysprofCallgraphView, functions_column_view);
  gtk_widget_class_bind_template_child (widget_class, SysprofCallgraphView, functions_name_sorter);
  gtk_widget_class_bind_template_child (widget_class, SysprofCallgraphView, paned);
  gtk_widget_class_bind_template_child (widget_class, SysprofCallgraphView, scrolled_window);
  gtk_widget_class_bind_template_callback (widget_class, sysprof_callgraph_view_key_pressed_cb);

  klass->augment_size = GLIB_SIZEOF_VOID_P;

  g_resources_register (libsysprof_gtk_get_resource ());
}

static void
sysprof_callgraph_view_init (SysprofCallgraphView *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
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
  g_autoptr(SysprofCallgraph) callgraph = NULL;
  g_autoptr(GError) error = NULL;
  GtkSorter *column_sorter;

  g_autoptr(GtkTreeListRowSorter) descendants_sorter = NULL;
  g_autoptr(GtkMultiSelection) descendants_selection = NULL;
  g_autoptr(GtkSortListModel) descendants_sort_model = NULL;
  g_autoptr(GtkTreeListModel) descendants_tree = NULL;
  g_autoptr(GtkTreeListRow) descendants_first = NULL;

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

  g_set_object (&self->callgraph, callgraph);

  column_sorter = gtk_column_view_get_sorter (self->descendants_column_view);
  descendants_tree = gtk_tree_list_model_new (g_object_ref (G_LIST_MODEL (callgraph)),
                                              FALSE, FALSE,
                                              sysprof_callgraph_view_create_model_func,
                                              NULL, NULL);
  descendants_sorter = gtk_tree_list_row_sorter_new (g_object_ref (column_sorter));
  descendants_sort_model = gtk_sort_list_model_new (g_object_ref (G_LIST_MODEL (descendants_tree)),
                                        g_object_ref (GTK_SORTER (descendants_sorter)));
  descendants_selection = gtk_multi_selection_new (g_object_ref (G_LIST_MODEL (descendants_sort_model)));
  gtk_column_view_set_model (self->descendants_column_view, GTK_SELECTION_MODEL (descendants_selection));

  column_sorter = gtk_column_view_get_sorter (self->functions_column_view);
  functions_model = sysprof_callgraph_list_symbols (callgraph);
  functions_sort_model = gtk_sort_list_model_new (g_object_ref (functions_model),
                                                  g_object_ref (column_sorter));
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

  gtk_custom_sorter_set_sort_func (self->functions_name_sorter,
                                   functions_name_compare, NULL, NULL);

  if (SYSPROF_CALLGRAPH_VIEW_GET_CLASS (self)->load)
   SYSPROF_CALLGRAPH_VIEW_GET_CLASS (self)->load (self, callgraph);

  if ((descendants_first = gtk_tree_list_model_get_row (descendants_tree, 0)))
    gtk_tree_list_row_set_expanded (descendants_first, TRUE);
}

static gboolean
sysprof_callgraph_view_reload (SysprofCallgraphView *self)
{
  g_assert (SYSPROF_IS_CALLGRAPH_VIEW (self));

  g_clear_handle_id (&self->reload_source, g_source_remove);

  sysprof_document_callgraph_async (self->document,
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

  gtk_column_view_set_model (self->descendants_column_view, NULL);
  gtk_column_view_set_model (self->functions_column_view, NULL);

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
      sysprof_callgraph_view_queue_reload (self);
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
