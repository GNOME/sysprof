/* sysprof-notebook.c
 *
 * Copyright 2019 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sysprof-notebook"

#include "config.h"

#include "sysprof-display.h"
#include "sysprof-notebook.h"
#include "sysprof-tab.h"

typedef struct
{
  void *dummy;
} SysprofNotebookPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (SysprofNotebook, sysprof_notebook, GTK_TYPE_NOTEBOOK)

enum {
  PROP_0,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

/**
 * sysprof_notebook_new:
 *
 * Create a new #SysprofNotebook.
 *
 * Returns: (transfer full): a newly created #SysprofNotebook
 *
 * Since: 3.34
 */
GtkWidget *
sysprof_notebook_new (void)
{
  return g_object_new (SYSPROF_TYPE_NOTEBOOK, NULL);
}

static void
sysprof_notebook_page_added (GtkNotebook *notebook,
                             GtkWidget   *child,
                             guint        page_num)
{
  g_assert (SYSPROF_IS_NOTEBOOK (notebook));
  g_assert (GTK_IS_WIDGET (child));

  if (SYSPROF_IS_DISPLAY (child))
    {
      GtkWidget *tab = sysprof_tab_new (SYSPROF_DISPLAY (child));

      gtk_notebook_set_tab_label (notebook, child, tab);
      gtk_notebook_set_tab_reorderable (notebook, child, TRUE);
    }

  gtk_notebook_set_show_tabs (notebook,
                              gtk_notebook_get_n_pages (notebook) > 1);
}

static void
sysprof_notebook_page_removed (GtkNotebook *notebook,
                               GtkWidget   *child,
                               guint        page_num)
{
  g_assert (SYSPROF_IS_NOTEBOOK (notebook));
  g_assert (GTK_IS_WIDGET (child));

  if (gtk_notebook_get_n_pages (notebook) == 0)
    {
      child = sysprof_display_new ();
      gtk_container_add (GTK_CONTAINER (notebook), child);
      gtk_widget_show (child);
    }

  gtk_notebook_set_show_tabs (notebook,
                              gtk_notebook_get_n_pages (notebook) > 1);
}

static void
sysprof_notebook_finalize (GObject *object)
{
  SysprofNotebook *self = (SysprofNotebook *)object;
  SysprofNotebookPrivate *priv = sysprof_notebook_get_instance_private (self);

  G_OBJECT_CLASS (sysprof_notebook_parent_class)->finalize (object);
}

static void
sysprof_notebook_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  SysprofNotebook *self = SYSPROF_NOTEBOOK (object);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_notebook_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  SysprofNotebook *self = SYSPROF_NOTEBOOK (object);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_notebook_class_init (SysprofNotebookClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkNotebookClass *notebook_class = GTK_NOTEBOOK_CLASS (klass);

  object_class->finalize = sysprof_notebook_finalize;
  object_class->get_property = sysprof_notebook_get_property;
  object_class->set_property = sysprof_notebook_set_property;

  notebook_class->page_added = sysprof_notebook_page_added;
  notebook_class->page_removed = sysprof_notebook_page_removed;
}

static void
sysprof_notebook_init (SysprofNotebook *self)
{
  gtk_notebook_set_show_border (GTK_NOTEBOOK (self), FALSE);
  gtk_notebook_set_scrollable (GTK_NOTEBOOK (self), TRUE);
  gtk_notebook_popup_enable (GTK_NOTEBOOK (self));
}

void
sysprof_notebook_close_current (SysprofNotebook *self)
{
  gint page;

  g_return_if_fail (SYSPROF_IS_NOTEBOOK (self));

  if ((page = gtk_notebook_get_current_page (GTK_NOTEBOOK (self))) >= 0)
    gtk_widget_destroy (gtk_notebook_get_nth_page (GTK_NOTEBOOK (self), page));
}
