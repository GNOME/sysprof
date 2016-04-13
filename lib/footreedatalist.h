/* gtktreedatalist.h
 * Copyright (C) 2000  Red Hat, Inc.,  Jonathan Blandford <jrb@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GTK_TREE_DATA_LIST_H__
#define __GTK_TREE_DATA_LIST_H__

#include <gtk/gtk.h>

typedef struct _FooTreeDataList FooTreeDataList;
struct _FooTreeDataList
{
  FooTreeDataList *next;

  union {
    gint	   v_int;
    gint8          v_char;
    guint8         v_uchar;
    guint	   v_uint;
    glong	   v_long;
    gulong	   v_ulong;
    gint64	   v_int64;
    guint64        v_uint64;
    gfloat	   v_float;
    gdouble        v_double;
    gpointer	   v_pointer;
  } data;
};

typedef struct _GtkTreeDataSortHeader
{
  gint sort_column_id;
  GtkTreeIterCompareFunc func;
  gpointer data;
  GDestroyNotify destroy;
} GtkTreeDataSortHeader;

FooTreeDataList *_foo_tree_data_list_alloc          (void);
void             _foo_tree_data_list_free           (FooTreeDataList *list,
						     GType           *column_headers);
gboolean         _foo_tree_data_list_check_type     (GType            type);
void             _foo_tree_data_list_node_to_value  (FooTreeDataList *list,
						     GType            type,
						     GValue          *value);
void             _foo_tree_data_list_value_to_node  (FooTreeDataList *list,
						     GValue          *value);

FooTreeDataList *_foo_tree_data_list_node_copy      (FooTreeDataList *list,
                                                     GType            type);

/* Header code */
gint                   _foo_tree_data_list_compare_func (GtkTreeModel *model,
							 GtkTreeIter  *a,
							 GtkTreeIter  *b,
							 gpointer      user_data);
GList *                _foo_tree_data_list_header_new  (gint          n_columns,
							GType        *types);
void                   _foo_tree_data_list_header_free (GList        *header_list);
GtkTreeDataSortHeader *_foo_tree_data_list_get_header  (GList        *header_list,
							gint          sort_column_id);
GList                 *_foo_tree_data_list_set_header  (GList                  *header_list,
							gint                    sort_column_id,
							GtkTreeIterCompareFunc  func,
							gpointer                data,
							GDestroyNotify          destroy);

#endif /* __GTK_TREE_DATA_LIST_H__ */
