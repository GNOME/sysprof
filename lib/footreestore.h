/* Copyright (C) 2000  Red Hat, Inc.,  Jonathan Blandford <jrb@redhat.com>
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

#ifndef __FOO_TREE_STORE_H__
#define __FOO_TREE_STORE_H__

#include <gtk/gtk.h>
#include <stdarg.h>


G_BEGIN_DECLS


#define FOO_TYPE_TREE_STORE			(foo_tree_store_get_type ())
#define FOO_TREE_STORE(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), FOO_TYPE_TREE_STORE, FooTreeStore))
#define FOO_TREE_STORE_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), FOO_TYPE_TREE_STORE, FooTreeStoreClass))
#define FOO_IS_TREE_STORE(obj)			(G_TYPE_CHECK_INSTANCE_TYPE ((obj), FOO_TYPE_TREE_STORE))
#define FOO_IS_TREE_STORE_CLASS(klass)		(G_TYPE_CHECK_CLASS_TYPE ((klass), FOO_TYPE_TREE_STORE))
#define FOO_TREE_STORE_GET_CLASS(obj)		(G_TYPE_INSTANCE_GET_CLASS ((obj), FOO_TYPE_TREE_STORE, FooTreeStoreClass))

typedef struct _FooTreeStore       FooTreeStore;
typedef struct _FooTreeStoreClass  FooTreeStoreClass;

struct _FooTreeStore
{
  GObject parent;

  gint stamp;
  gpointer root;
  gpointer last;
  gint n_columns;
  gint sort_column_id;
  GList *sort_list;
  GtkSortType order;
  GType *column_headers;
  GtkTreeIterCompareFunc default_sort_func;
  gpointer default_sort_data;
  GDestroyNotify default_sort_destroy;

  guint row_changed_id;
  guint row_inserted_id;
  guint row_has_child_toggled_id;
  guint rows_reordered_id;
  
  guint (columns_dirty) : 1;
};

struct _FooTreeStoreClass
{
  GObjectClass parent_class;

  /* Padding for future expansion */
  void (*_gtk_reserved1) (void);
  void (*_gtk_reserved2) (void);
  void (*_gtk_reserved3) (void);
  void (*_gtk_reserved4) (void);
};


GType         foo_tree_store_get_type         (void) G_GNUC_CONST;
FooTreeStore *foo_tree_store_new              (gint          n_columns,
					       ...);
FooTreeStore *foo_tree_store_newv             (gint          n_columns,
					       GType        *types);
void          foo_tree_store_set_column_types (FooTreeStore *tree_store,
					       gint          n_columns,
					       GType        *types);

/* NOTE: use gtk_tree_model_get to get values from a FooTreeStore */

void          foo_tree_store_set_value        (FooTreeStore *tree_store,
					       GtkTreeIter  *iter,
					       gint          column,
					       GValue       *value);
void          foo_tree_store_set              (FooTreeStore *tree_store,
					       GtkTreeIter  *iter,
					       ...);
void          foo_tree_store_set_valuesv      (FooTreeStore *tree_store,
					       GtkTreeIter  *iter,
					       gint         *columns,
					       GValue       *values,
					       gint          n_values);
void          foo_tree_store_set_valist       (FooTreeStore *tree_store,
					       GtkTreeIter  *iter,
					       va_list       var_args);
gboolean      foo_tree_store_remove           (FooTreeStore *tree_store,
					       GtkTreeIter  *iter);
void          foo_tree_store_insert           (FooTreeStore *tree_store,
					       GtkTreeIter  *iter,
					       GtkTreeIter  *parent,
					       gint          position);
void          foo_tree_store_insert_before    (FooTreeStore *tree_store,
					       GtkTreeIter  *iter,
					       GtkTreeIter  *parent,
					       GtkTreeIter  *sibling);
void          foo_tree_store_insert_after     (FooTreeStore *tree_store,
					       GtkTreeIter  *iter,
					       GtkTreeIter  *parent,
					       GtkTreeIter  *sibling);
void          foo_tree_store_insert_with_values (FooTreeStore *tree_store,
						 GtkTreeIter  *iter,
						 GtkTreeIter  *parent,
						 gint          position,
						 ...);
void          foo_tree_store_insert_with_valuesv (FooTreeStore *tree_store,
						  GtkTreeIter  *iter,
						  GtkTreeIter  *parent,
						  gint          position,
						  gint         *columns,
						  GValue       *values,
						  gint          n_values);
void          foo_tree_store_prepend          (FooTreeStore *tree_store,
					       GtkTreeIter  *iter,
					       GtkTreeIter  *parent);
void          foo_tree_store_append           (FooTreeStore *tree_store,
					       GtkTreeIter  *iter,
					       GtkTreeIter  *parent);
gboolean      foo_tree_store_is_ancestor      (FooTreeStore *tree_store,
					       GtkTreeIter  *iter,
					       GtkTreeIter  *descendant);
gint          foo_tree_store_iter_depth       (FooTreeStore *tree_store,
					       GtkTreeIter  *iter);
void          foo_tree_store_clear            (FooTreeStore *tree_store);
gboolean      foo_tree_store_iter_is_valid    (FooTreeStore *tree_store,
                                               GtkTreeIter  *iter);
void          foo_tree_store_reorder          (FooTreeStore *tree_store,
                                               GtkTreeIter  *parent,
                                               gint         *new_order);
void          foo_tree_store_swap             (FooTreeStore *tree_store,
                                               GtkTreeIter  *a,
                                               GtkTreeIter  *b);
void          foo_tree_store_move_before      (FooTreeStore *tree_store,
                                               GtkTreeIter  *iter,
                                               GtkTreeIter  *position);
void          foo_tree_store_move_after       (FooTreeStore *tree_store,
                                               GtkTreeIter  *iter,
                                               GtkTreeIter  *position);


G_END_DECLS


#endif /* __FOO_TREE_STORE_H__ */
