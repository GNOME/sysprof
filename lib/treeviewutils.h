/* -*- mode: C; c-file-style: "linux" -*- */

/* MemProf -- memory profiler and leak detector
 * Copyright 2002, Soeren Sandmann (sandmann@daimi.au.dk)
 * Copyright 2003, 2004, Red Hat, Inc.
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

#include <gtk/gtk.h>

GtkTreeViewColumn *add_plain_text_column    (GtkTreeView *view,
					     const char  *title,
					     gint         model_column);
GtkTreeViewColumn *add_double_format_column (GtkTreeView *view,
					     const char  *title,
					     int          model_column,
					     const char  *format);
GtkTreeViewColumn *add_pointer_column       (GtkTreeView *view,
					     const char  *title,
					     int          model_column);
void		   tree_view_set_model_with_default_sort (GtkTreeView *view,
							  GtkTreeModel *model,
							  int model_column,
							  GtkSortType default_sort);
void		   tree_view_set_sort_column (GtkTreeView *view,
					      int          model_column,
					      int          sort_type);

typedef void (* VisibleCallback) (GtkTreeView  *view,
				  GtkTreePath  *path,
				  GtkTreeIter  *iter,
				  gpointer      data);
void tree_view_foreach_visible (GtkTreeView *view,
				VisibleCallback callback,
				gpointer data);



void
set_error_va (GError **err, gint domain, gint code,
	      const char *format, va_list args);
