/* -*- mode: C; c-file-style: "linux" -*- */

/* MemProf -- memory profiler and leak detector
 * Copyright 2002, Soeren Sandmann (sandmann@daimi.au.dk)
 * Copyright 2003, 2004, Red Hat, Inc.
 *
 * Sysprof -- Sampling, systemwide CPU profiler 
 * Copyright 2004, 2005, Soeren Sandmann
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

void
column_set_sort_id (GtkTreeViewColumn *column,
		    int                column_id)
{
	g_object_set_data (G_OBJECT (column),
			   "mi-saved-sort-column", GINT_TO_POINTER (column_id));
	gtk_tree_view_column_set_sort_column_id (column, column_id);
}

void
tree_view_unset_sort_ids (GtkTreeView *tree_view)
{
	GList *columns = gtk_tree_view_get_columns (tree_view);
	GList *l;

	for (l = columns; l; l = l->next) {
		gtk_tree_view_column_set_sort_column_id (l->data, -1);
	}

	g_list_free (columns);
}

void
tree_view_set_sort_ids (GtkTreeView *tree_view)
{
	GList *columns = gtk_tree_view_get_columns (tree_view);
	GList *l;

	for (l = columns; l; l = l->next) {
		int column_id = GPOINTER_TO_INT (g_object_get_data (l->data, "mi-saved-sort-column"));
		gtk_tree_view_column_set_sort_column_id (l->data, column_id);
	}

	g_list_free (columns);
}

int
list_iter_get_index (GtkTreeModel *model,
		     GtkTreeIter  *iter)
{
	GtkTreePath *path = gtk_tree_model_get_path (model,iter);
	int result;
	
	g_assert (path);
	g_assert (gtk_tree_path_get_depth (path) == 1);
	result = gtk_tree_path_get_indices (path)[0];
	gtk_tree_path_free (path);

	return result;

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
	column_set_sort_id (column, model_column);

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

        g_object_set (cell, "text", text, NULL);
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
add_double_format_column (GtkTreeView *view, const gchar *title, gint model_column, const char *format)
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
	
	gtk_tree_view_column_set_cell_data_func (column, renderer,
						 double_to_text, column_info, free_column_info);

	gtk_tree_view_append_column (view, column);
	column_set_sort_id (column, model_column);
	
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
	gtk_tree_view_column_set_cell_data_func (column, renderer,
						 pointer_to_text, GINT_TO_POINTER (model_column), NULL);

	gtk_tree_view_append_column (view, column);
	column_set_sort_id (column, model_column);
	
	return column;
}

typedef struct
{
	gboolean	is_sorted;
	int		sort_column;
	GtkSortType	sort_type;
} SortState;


gpointer
save_sort_state (GtkTreeView *view)
{
	SortState *state = NULL;
	GtkTreeModel *model = gtk_tree_view_get_model (view);

	if (model && GTK_IS_TREE_SORTABLE (model)) {
		state = g_new (SortState, 1);
		state->is_sorted = gtk_tree_sortable_get_sort_column_id (
			GTK_TREE_SORTABLE (model),
			&(state->sort_column),
			&(state->sort_type));
	}
	return state;
}

void
restore_sort_state (GtkTreeView *view, gpointer st)
{
	SortState *state = st;
	GtkTreeModel *model = gtk_tree_view_get_model (view);

	if (state) {
		if (state->is_sorted && model && GTK_IS_TREE_SORTABLE (model)) {
			gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (model),
							      state->sort_column,
							      state->sort_type);
		}
		g_free (state);
	}
	
	gtk_tree_sortable_sort_column_changed (GTK_TREE_SORTABLE (model));
}
