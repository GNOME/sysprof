/* MemProf -- memory profiler and leak detector
 * Copyright 2002, Soeren Sandmann (sandmann@daimi.au.dk)
 * Copyright 2003, 2004, Red Hat, Inc.
 *
 * Sysprof -- Sampling, systemwide CPU profiler 
 * Copyright 2004, 2005, 2006, 2007, 2008 Soeren Sandmann
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
/*====*/

#include "treeviewutils.h"

static void on_column_clicked (GtkTreeViewColumn *column,
			       gpointer data);
typedef struct
{
    int		model_column;
    GtkSortType	default_order;
} SortInfo;

static void
set_sort_info (GtkTreeView       *view,
               GtkTreeViewColumn *column,
               int                model_column,
               GtkSortType        default_order)
{
    SortInfo *info;
    
    info = g_new0 (SortInfo, 1);
    info->model_column = model_column;
    info->default_order = default_order;
    
    gtk_tree_view_column_set_clickable (column, TRUE);
    
    g_object_set_data (G_OBJECT (column), "column_info", info);
    g_signal_connect (column, "clicked", G_CALLBACK (on_column_clicked), view);
}

static SortInfo *
get_sort_info (GtkTreeViewColumn *column)
{
    return g_object_get_data (G_OBJECT (column), "column_info");
}

static GtkTreeViewColumn *
find_column_by_model_column (GtkTreeView *view,
			     int          model_column)
{
    GList *columns = gtk_tree_view_get_columns (view);
    GList *list;
    GtkTreeViewColumn *result = NULL;
    
    for (list = columns; list != NULL; list = list->next)
    {
	GtkTreeViewColumn *column = list->data;
	SortInfo *info = get_sort_info (column);
	
	if (info->model_column == model_column)
	    result = column;
    }
    
    g_list_free (columns);
    
    return result;
}

void
tree_view_set_sort_column (GtkTreeView *view,
			   int          model_column,
			   int          sort_type)
{
    GList *columns, *list;
    GtkTreeSortable *sortable;
    GtkTreeViewColumn *column = find_column_by_model_column (view, model_column);
    SortInfo *info = get_sort_info (column);
    
    /* For each column in the view, unset the sort indicator */
    columns = gtk_tree_view_get_columns (view);
    for (list = columns; list != NULL; list = list->next)
    {
	GtkTreeViewColumn *col = GTK_TREE_VIEW_COLUMN (list->data);
	
	gtk_tree_view_column_set_sort_indicator (col, FALSE);
    }
    
    /* Set the sort indicator for this column */
    gtk_tree_view_column_set_sort_indicator (column, TRUE);
    gtk_tree_view_column_set_sort_order (column, sort_type);
    
    /* And sort the column */
    sortable = GTK_TREE_SORTABLE (gtk_tree_view_get_model (view));
    
    gtk_tree_sortable_set_sort_column_id (sortable,
					  info->model_column,
					  sort_type);
}

static void
on_column_clicked (GtkTreeViewColumn *column,
		   gpointer data)
{
    GtkTreeView *view = data;
    GtkSortType sort_type;
    SortInfo *info = get_sort_info (column);
    
    /* Find out what our current sort indicator is. If it is NONE, then
     * select the default for the column, otherwise, select the opposite
     */
    if (!gtk_tree_view_column_get_sort_indicator (column))
    {
	sort_type = info->default_order;
    }
    else
    {
	if (gtk_tree_view_column_get_sort_order (column) == GTK_SORT_ASCENDING)
	    sort_type = GTK_SORT_DESCENDING;
	else
	    sort_type = GTK_SORT_ASCENDING;
    }
    
    tree_view_set_sort_column (view, info->model_column, sort_type);
}

GtkTreeViewColumn *
add_plain_text_column (GtkTreeView *view, const gchar *title, gint model_column)
{
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    
    renderer = gtk_cell_renderer_text_new ();
    g_object_set (renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    column = gtk_tree_view_column_new_with_attributes (title, renderer,
						       "text", model_column,
						       NULL);
    gtk_tree_view_column_set_resizable (column, TRUE);
    gtk_tree_view_append_column (view, column);
    
    set_sort_info (view, column, model_column, GTK_SORT_ASCENDING);
    
    return column;
}

static void
pointer_to_text (GtkTreeViewColumn *tree_column,
		 GtkCellRenderer *cell, GtkTreeModel *tree_model,
		 GtkTreeIter *iter, gpointer data)
{
    gpointer p;
    gchar *text;
    int column = GPOINTER_TO_INT (data);
    
    gtk_tree_model_get (tree_model, iter, column, &p, -1);
    text = g_strdup_printf ("%p", p);
    g_object_set (cell, "text", text, NULL);
    g_free (text);
}

typedef struct {
    int column;
    char *format;
} ColumnInfo;

static void
double_to_text (GtkTreeViewColumn *tree_column,
		GtkCellRenderer *cell, GtkTreeModel *tree_model,
		GtkTreeIter *iter, gpointer data)
{
    gdouble d;
    gchar *text;
    ColumnInfo *info = data;
    
    gtk_tree_model_get (tree_model, iter, info->column, &d, -1);
    
    text = g_strdup_printf (info->format, d);
    
    g_object_set (cell, "markup", text, NULL);
    g_free (text);
}

static void
free_column_info (void *data)
{
    ColumnInfo *info = data;
    g_free (info->format);
    g_free (info);
}

GtkTreeViewColumn *
add_double_format_column (GtkTreeView *view,
			  const gchar *title,
			  gint         model_column,
			  const char  *format)
{
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    ColumnInfo *column_info = g_new (ColumnInfo, 1);
    
    renderer = gtk_cell_renderer_text_new ();
    g_object_set (renderer, "xalign", 1.0, NULL);
    
    column = gtk_tree_view_column_new ();
    gtk_tree_view_column_set_title  (column, title);
    gtk_tree_view_column_pack_start (column, renderer, TRUE);
    gtk_tree_view_column_set_resizable (column, FALSE);
    
    column_info->column = model_column;
    column_info->format = g_strdup (format);
    
    gtk_tree_view_column_set_cell_data_func (
	column, renderer, double_to_text, column_info, free_column_info);
    
    gtk_tree_view_append_column (view, column);
    
    set_sort_info (view, column, model_column, GTK_SORT_DESCENDING);
    
    return column;
}

GtkTreeViewColumn *
add_pointer_column (GtkTreeView *view, const gchar *title, gint model_column)
{
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    
    renderer = gtk_cell_renderer_text_new ();
    
    column = gtk_tree_view_column_new ();
    if (title)
	gtk_tree_view_column_set_title  (column, title);
    gtk_tree_view_column_pack_start (column, renderer, TRUE);
    gtk_tree_view_column_set_resizable (column, TRUE);
    gtk_tree_view_column_set_cell_data_func (
	column, renderer, pointer_to_text, GINT_TO_POINTER (model_column), NULL);
    
    gtk_tree_view_append_column (view, column);
    
    return column;
}

void
tree_view_set_model_with_default_sort (GtkTreeView  *view,
				       GtkTreeModel *model,
				       int           model_column,
				       GtkSortType   default_sort)
{
    int		     column;
    GtkSortType	     type;
    GtkTreeSortable *old_model;
    GtkAdjustment   *adjustment;
    
    old_model = GTK_TREE_SORTABLE (gtk_tree_view_get_model (view));
    
    if (!(old_model && gtk_tree_sortable_get_sort_column_id (
	      GTK_TREE_SORTABLE (old_model), &column, &type)))
    {
	column = model_column;
	type = default_sort;
    }

    /* Setting the sort column here prevents the "rows_reordered"
     * signal from being emitted when the model is attached to
     * a treeview. This is desirable because at that point there
     * is a handler attached, which means the signal will actually
     * be emitted.
     */
    gtk_tree_sortable_set_sort_column_id (
	GTK_TREE_SORTABLE (model), column, type);

    gtk_tree_view_set_model (view, model);

    tree_view_set_sort_column (view, column, type);
    
    /* Workaround for GTK+ crack, see bug 405625 */
    adjustment = gtk_tree_view_get_vadjustment (view);
    if (adjustment)
	gtk_adjustment_set_value (adjustment, 0);
}

static void
process_iter (GtkTreeView      *view,
	      GtkTreeIter      *iter,
	      VisibleCallback   callback,
	      gpointer          data)
{
    GtkTreeModel *model = gtk_tree_view_get_model (view);
    GtkTreePath *path;
    GtkTreeIter child;
    
    path = gtk_tree_model_get_path (model, iter);
    
    callback (view, path, iter, data);
    
    if (gtk_tree_view_row_expanded (view, path))
    {
	if (gtk_tree_model_iter_children (model, &child, iter))
	{
	    do
	    {
		process_iter (view, &child, callback, data);
	    }
	    while (gtk_tree_model_iter_next (model, &child));
	}
    }
    
    gtk_tree_path_free (path);
}

void
tree_view_foreach_visible (GtkTreeView *view,
			   VisibleCallback callback,
			   gpointer data)
{
    GtkTreeModel *model = gtk_tree_view_get_model (view);
    GtkTreeIter iter;
    
    if (model && gtk_tree_model_get_iter_first (model, &iter))
	process_iter (view, &iter, callback, data);
}

/* Not really a *treeview* utility, but there isn't really a
 * better place to put it
 */
void
set_error (GError **err, gint domain, gint code,
	   const char *format, va_list args)
{
    char *msg;
    
    if (!err)
        return;
    
    msg = g_strdup_vprintf (format, args);
    
    if (*err == NULL)
    {
        *err = g_error_new_literal (G_MARKUP_ERROR, code, msg);
    }
    else
    {
        /* Warning text from GLib */
        g_warning ("GError set over the top of a previous GError or uninitialized memory.\n" 
                   "This indicates a bug in someone's code. You must ensure an error is NULL before it's set.\n"
                   "The overwriting error message was: %s",
                   msg);
    }
    
    g_free (msg);
}

