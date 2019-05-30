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
#include "sysprof-ui-private.h"

typedef struct
{
  guint always_show_tabs : 1;
} SysprofNotebookPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (SysprofNotebook, sysprof_notebook, GTK_TYPE_NOTEBOOK)

enum {
  PROP_0,
  PROP_ALWAYS_SHOW_TABS,
  PROP_CAN_REPLAY,
  PROP_CAN_SAVE,
  PROP_CURRENT,
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
sysprof_notebook_notify_can_save_cb (SysprofNotebook *self,
                                     GParamSpec      *pspec,
                                     SysprofDisplay  *display)
{
  g_assert (SYSPROF_IS_NOTEBOOK (self));
  g_assert (SYSPROF_IS_DISPLAY (display));

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CAN_SAVE]);
}

static void
sysprof_notebook_notify_can_replay_cb (SysprofNotebook *self,
                                       GParamSpec      *pspec,
                                       SysprofDisplay  *display)
{
  g_assert (SYSPROF_IS_NOTEBOOK (self));
  g_assert (SYSPROF_IS_DISPLAY (display));

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CAN_REPLAY]);
}

static void
sysprof_notebook_page_added (GtkNotebook *notebook,
                             GtkWidget   *child,
                             guint        page_num)
{
  SysprofNotebook *self = (SysprofNotebook *)notebook;
  SysprofNotebookPrivate *priv = sysprof_notebook_get_instance_private (self);

  g_assert (SYSPROF_IS_NOTEBOOK (self));
  g_assert (GTK_IS_WIDGET (child));

  gtk_notebook_set_show_tabs (notebook,
                              (priv->always_show_tabs ||
                               gtk_notebook_get_n_pages (notebook) > 1));

  if (SYSPROF_IS_DISPLAY (child))
    {
      GtkWidget *tab = sysprof_tab_new (SYSPROF_DISPLAY (child));

      gtk_notebook_set_tab_label (notebook, child, tab);
      gtk_notebook_set_tab_reorderable (notebook, child, TRUE);

      g_signal_connect_object (child,
                               "notify::can-replay",
                               G_CALLBACK (sysprof_notebook_notify_can_replay_cb),
                               notebook,
                               G_CONNECT_SWAPPED);

      g_signal_connect_object (child,
                               "notify::can-save",
                               G_CALLBACK (sysprof_notebook_notify_can_save_cb),
                               notebook,
                               G_CONNECT_SWAPPED);

      g_object_notify_by_pspec (G_OBJECT (notebook), properties [PROP_CAN_REPLAY]);
      g_object_notify_by_pspec (G_OBJECT (notebook), properties [PROP_CAN_SAVE]);
      g_object_notify_by_pspec (G_OBJECT (notebook), properties [PROP_CURRENT]);

      _sysprof_display_focus_record (SYSPROF_DISPLAY (child));
    }
}

static void
sysprof_notebook_page_removed (GtkNotebook *notebook,
                               GtkWidget   *child,
                               guint        page_num)
{
  SysprofNotebook *self = (SysprofNotebook *)notebook;
  SysprofNotebookPrivate *priv = sysprof_notebook_get_instance_private (self);

  g_assert (SYSPROF_IS_NOTEBOOK (self));
  g_assert (GTK_IS_WIDGET (child));

  if (gtk_widget_in_destruction (GTK_WIDGET (notebook)))
    return;

  if (gtk_notebook_get_n_pages (notebook) == 0)
    {
      child = sysprof_display_new ();
      gtk_container_add (GTK_CONTAINER (notebook), child);
      gtk_widget_show (child);

      g_signal_handlers_disconnect_by_func (child,
                                            G_CALLBACK (sysprof_notebook_notify_can_save_cb),
                                            notebook);

      g_object_notify_by_pspec (G_OBJECT (notebook), properties [PROP_CAN_REPLAY]);
      g_object_notify_by_pspec (G_OBJECT (notebook), properties [PROP_CAN_SAVE]);
      g_object_notify_by_pspec (G_OBJECT (notebook), properties [PROP_CURRENT]);
    }

  gtk_notebook_set_show_tabs (notebook,
                              (priv->always_show_tabs ||
                               gtk_notebook_get_n_pages (notebook) > 1));
}

static void
sysprof_notebook_switch_page (GtkNotebook *notebook,
                              GtkWidget   *widget,
                              guint        page)
{
  g_assert (GTK_IS_NOTEBOOK (notebook));
  g_assert (GTK_IS_WIDGET (widget));

  GTK_NOTEBOOK_CLASS (sysprof_notebook_parent_class)->switch_page (notebook, widget, page);

  g_object_notify_by_pspec (G_OBJECT (notebook), properties [PROP_CAN_REPLAY]);
  g_object_notify_by_pspec (G_OBJECT (notebook), properties [PROP_CAN_SAVE]);
  g_object_notify_by_pspec (G_OBJECT (notebook), properties [PROP_CURRENT]);
}

static void
sysprof_notebook_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  SysprofNotebook *self = (SysprofNotebook *)object;

  switch (prop_id)
    {
    case PROP_ALWAYS_SHOW_TABS:
      g_value_set_boolean (value, sysprof_notebook_get_always_show_tabs (self));
      break;

    case PROP_CAN_REPLAY:
      g_value_set_boolean (value, sysprof_notebook_get_can_replay (self));
      break;

    case PROP_CAN_SAVE:
      g_value_set_boolean (value, sysprof_notebook_get_can_save (self));
      break;

    case PROP_CURRENT:
      g_value_set_object (value, sysprof_notebook_get_current (self));
      break;

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
  SysprofNotebook *self = (SysprofNotebook *)object;

  switch (prop_id)
    {
    case PROP_ALWAYS_SHOW_TABS:
      sysprof_notebook_set_always_show_tabs (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_notebook_class_init (SysprofNotebookClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkNotebookClass *notebook_class = GTK_NOTEBOOK_CLASS (klass);

  object_class->get_property = sysprof_notebook_get_property;
  object_class->set_property = sysprof_notebook_set_property;

  notebook_class->page_added = sysprof_notebook_page_added;
  notebook_class->page_removed = sysprof_notebook_page_removed;
  notebook_class->switch_page = sysprof_notebook_switch_page;

  properties [PROP_ALWAYS_SHOW_TABS] =
    g_param_spec_boolean ("always-show-tabs",
                          "Always Show Tabs",
                          "Always Show Tabs",
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_CAN_REPLAY] =
    g_param_spec_boolean ("can-replay",
                          "Can Replay",
                          "If the current display can replay a recording",
                          FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_CAN_SAVE] =
    g_param_spec_boolean ("can-save",
                          "Can Save",
                          "If the current display can save a recording",
                          FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_CURRENT] =
    g_param_spec_object ("current",
                         "Current",
                         "The current display",
                         SYSPROF_TYPE_DISPLAY,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
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

static void
find_empty_display_cb (GtkWidget *widget,
                       gpointer   user_data)
{
  GtkWidget **display = user_data;

  g_assert (GTK_IS_WIDGET (widget));
  g_assert (display != NULL);

  if (*display != NULL)
    return;

  if (SYSPROF_IS_DISPLAY (widget) &&
      sysprof_display_is_empty (SYSPROF_DISPLAY (widget)))
    *display = widget;
}

void
sysprof_notebook_open (SysprofNotebook *self,
                       GFile           *file)
{
  GtkWidget *display = NULL;
  gint page;

  g_return_if_fail (SYSPROF_IS_NOTEBOOK (self));
  g_return_if_fail (g_file_is_native (file));

  gtk_container_foreach (GTK_CONTAINER (self),
                         find_empty_display_cb,
                         &display);

  if (display == NULL)
    {

      display = sysprof_display_new ();
      page = gtk_notebook_insert_page (GTK_NOTEBOOK (self), display, NULL, -1);
      gtk_widget_show (display);
    }
  else
    {
      page = gtk_notebook_page_num (GTK_NOTEBOOK (self), display);
    }

  gtk_notebook_set_current_page (GTK_NOTEBOOK (self), page);

  sysprof_display_open (SYSPROF_DISPLAY (display), file);
}

SysprofDisplay *
sysprof_notebook_get_current (SysprofNotebook *self)
{
  gint page;

  g_assert (SYSPROF_IS_NOTEBOOK (self));

  if ((page = gtk_notebook_get_current_page (GTK_NOTEBOOK (self))) >= 0)
    return SYSPROF_DISPLAY (gtk_notebook_get_nth_page (GTK_NOTEBOOK (self), page));

  return NULL;
}

void
sysprof_notebook_save (SysprofNotebook *self)
{
  SysprofDisplay *display;

  g_return_if_fail (SYSPROF_IS_NOTEBOOK (self));

  if ((display = sysprof_notebook_get_current (self)))
    sysprof_display_save (display);
}

gboolean
sysprof_notebook_get_can_save (SysprofNotebook *self)
{
  SysprofDisplay *display;

  g_return_val_if_fail (SYSPROF_IS_NOTEBOOK (self), FALSE);

  if ((display = sysprof_notebook_get_current (self)))
    return sysprof_display_get_can_save (display);

  return FALSE;
}

gboolean
sysprof_notebook_get_can_replay (SysprofNotebook *self)
{
  SysprofDisplay *display;

  g_return_val_if_fail (SYSPROF_IS_NOTEBOOK (self), FALSE);

  if ((display = sysprof_notebook_get_current (self)))
    return sysprof_display_get_can_replay (display);

  return FALSE;
}

void
sysprof_notebook_replay (SysprofNotebook *self)
{
  SysprofDisplay *display;
  SysprofDisplay *replay;
  gint page;

  g_return_if_fail (SYSPROF_IS_NOTEBOOK (self));

  if (!(display = sysprof_notebook_get_current (self)) ||
      !sysprof_display_get_can_replay (display) ||
      !(replay = sysprof_display_replay (display)))
    return;

  g_return_if_fail (SYSPROF_IS_DISPLAY (replay));

  gtk_widget_show (GTK_WIDGET (replay));
  gtk_container_add (GTK_CONTAINER (self), GTK_WIDGET (replay));
  page = gtk_notebook_page_num (GTK_NOTEBOOK (self), GTK_WIDGET (replay));
  gtk_notebook_set_current_page (GTK_NOTEBOOK (self), page);
}

void
sysprof_notebook_add_profiler (SysprofNotebook *self,
                               SysprofProfiler *profiler)
{
  GtkWidget *display;
  gint page;

  g_return_if_fail (SYSPROF_IS_NOTEBOOK (self));
  g_return_if_fail (SYSPROF_IS_PROFILER (profiler));

  display = sysprof_display_new_for_profiler (profiler);

  gtk_widget_show (display);
  gtk_container_add (GTK_CONTAINER (self), display);
  page = gtk_notebook_page_num (GTK_NOTEBOOK (self), display);
  gtk_notebook_set_current_page (GTK_NOTEBOOK (self), page);
}

gboolean
sysprof_notebook_get_always_show_tabs (SysprofNotebook *self)
{
  SysprofNotebookPrivate *priv = sysprof_notebook_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_NOTEBOOK (self), FALSE);

  return priv->always_show_tabs;
}

void
sysprof_notebook_set_always_show_tabs (SysprofNotebook *self,
                                       gboolean         always_show_tabs)
{
  SysprofNotebookPrivate *priv = sysprof_notebook_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_NOTEBOOK (self));

  always_show_tabs = !!always_show_tabs;

  if (always_show_tabs != priv->always_show_tabs)
    {
      priv->always_show_tabs = always_show_tabs;
      gtk_notebook_set_show_tabs (GTK_NOTEBOOK (self),
                                  (priv->always_show_tabs ||
                                   gtk_notebook_get_n_pages (GTK_NOTEBOOK (self)) > 1));
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_ALWAYS_SHOW_TABS]);
    }
}
