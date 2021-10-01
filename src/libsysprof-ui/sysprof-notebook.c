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
  GtkNotebook *notebook;
  guint always_show_tabs : 1;
} SysprofNotebookPrivate;

static void buildable_iface_init (GtkBuildableIface *iface);

G_DEFINE_TYPE_WITH_CODE (SysprofNotebook, sysprof_notebook, GTK_TYPE_WIDGET,
                         G_ADD_PRIVATE (SysprofNotebook)
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, buildable_iface_init))

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
sysprof_notebook_page_added (SysprofNotebook *self,
                             GtkWidget       *child,
                             guint            page_num,
                             GtkNotebook     *notebook)
{
  SysprofNotebookPrivate *priv = sysprof_notebook_get_instance_private (self);

  g_assert (SYSPROF_IS_NOTEBOOK (self));
  g_assert (GTK_IS_WIDGET (child));
  g_assert (GTK_IS_NOTEBOOK (notebook));

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
                               self,
                               G_CONNECT_SWAPPED);

      g_signal_connect_object (child,
                               "notify::can-save",
                               G_CALLBACK (sysprof_notebook_notify_can_save_cb),
                               self,
                               G_CONNECT_SWAPPED);

      g_object_notify_by_pspec (G_OBJECT (notebook), properties [PROP_CAN_REPLAY]);
      g_object_notify_by_pspec (G_OBJECT (notebook), properties [PROP_CAN_SAVE]);
      g_object_notify_by_pspec (G_OBJECT (notebook), properties [PROP_CURRENT]);

      _sysprof_display_focus_record (SYSPROF_DISPLAY (child));
    }
}

static void
sysprof_notebook_page_removed (SysprofNotebook *self,
                               GtkWidget       *child,
                               guint            page_num,
                               GtkNotebook     *notebook)
{
  SysprofNotebookPrivate *priv = sysprof_notebook_get_instance_private (self);

  g_assert (SYSPROF_IS_NOTEBOOK (self));
  g_assert (GTK_IS_WIDGET (child));
  g_assert (GTK_IS_NOTEBOOK (notebook));

  if (gtk_widget_in_destruction (GTK_WIDGET (notebook)))
    return;

  if (gtk_notebook_get_n_pages (notebook) == 0)
    {
      child = sysprof_display_new ();
      gtk_notebook_append_page (notebook, child, NULL);
      gtk_widget_show (child);

      g_signal_handlers_disconnect_by_func (child,
                                            G_CALLBACK (sysprof_notebook_notify_can_save_cb),
                                            notebook);

      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CAN_REPLAY]);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CAN_SAVE]);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CURRENT]);
    }

  gtk_notebook_set_show_tabs (notebook,
                              (priv->always_show_tabs ||
                               gtk_notebook_get_n_pages (notebook) > 1));
}

static void
sysprof_notebook_switch_page (SysprofNotebook *self,
                              GtkWidget       *widget,
                              guint            page,
                              GtkNotebook     *notebook)
{
  g_assert (SYSPROF_IS_NOTEBOOK (self));
  g_assert (GTK_IS_NOTEBOOK (notebook));
  g_assert (GTK_IS_WIDGET (widget));

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CAN_REPLAY]);
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CAN_SAVE]);
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CURRENT]);
}

static void
sysprof_notebook_dispose (GObject *object)
{
  SysprofNotebook *self = (SysprofNotebook *)object;
  SysprofNotebookPrivate *priv = sysprof_notebook_get_instance_private (self);

  if (priv->notebook)
    {
      gtk_widget_unparent (GTK_WIDGET (priv->notebook));
      priv->notebook = NULL;
    }

  G_OBJECT_CLASS (sysprof_notebook_parent_class)->dispose (object);
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
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_notebook_dispose;
  object_class->get_property = sysprof_notebook_get_property;
  object_class->set_property = sysprof_notebook_set_property;

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

  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
}

static void
sysprof_notebook_init (SysprofNotebook *self)
{
  SysprofNotebookPrivate *priv = sysprof_notebook_get_instance_private (self);

  priv->notebook = GTK_NOTEBOOK (gtk_notebook_new ());
  gtk_widget_set_parent (GTK_WIDGET (priv->notebook), GTK_WIDGET (self));

  gtk_notebook_set_show_border (priv->notebook, FALSE);
  gtk_notebook_set_scrollable (priv->notebook, TRUE);
  gtk_notebook_popup_enable (priv->notebook);

  g_signal_connect_object (priv->notebook,
                           "page-added",
                           G_CALLBACK (sysprof_notebook_page_added),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (priv->notebook,
                           "page-removed",
                           G_CALLBACK (sysprof_notebook_page_removed),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (priv->notebook,
                           "switch-page",
                           G_CALLBACK (sysprof_notebook_switch_page),
                           self,
                           G_CONNECT_SWAPPED | G_CONNECT_AFTER);
}

void
sysprof_notebook_close_current (SysprofNotebook *self)
{
  SysprofNotebookPrivate *priv = sysprof_notebook_get_instance_private (self);
  gint page;

  g_return_if_fail (SYSPROF_IS_NOTEBOOK (self));

  if ((page = gtk_notebook_get_current_page (priv->notebook)) >= 0)
    gtk_notebook_remove_page (priv->notebook, page);
}

void
sysprof_notebook_open (SysprofNotebook *self,
                       GFile           *file)
{
  SysprofNotebookPrivate *priv = sysprof_notebook_get_instance_private (self);
  SysprofDisplay *display = NULL;
  int page;

  g_return_if_fail (SYSPROF_IS_NOTEBOOK (self));
  g_return_if_fail (g_file_is_native (file));

  for (page = 0; page < sysprof_notebook_get_n_pages (self); page++)
    {
      SysprofDisplay *child = sysprof_notebook_get_nth_page (self, page);

      if (sysprof_display_is_empty (child))
        {
          display = child;
          break;
        }
    }

  if (display == NULL)
    {
      display = SYSPROF_DISPLAY (sysprof_display_new ());
      page = sysprof_notebook_append (self, display);
    }
  else
    {
      page = gtk_notebook_page_num (priv->notebook, GTK_WIDGET (display));
    }

  sysprof_notebook_set_current_page (self, page);

  sysprof_display_open (SYSPROF_DISPLAY (display), file);
}

SysprofDisplay *
sysprof_notebook_get_current (SysprofNotebook *self)
{
  SysprofNotebookPrivate *priv = sysprof_notebook_get_instance_private (self);
  gint page;

  g_assert (SYSPROF_IS_NOTEBOOK (self));

  if ((page = gtk_notebook_get_current_page (priv->notebook)) >= 0)
    return SYSPROF_DISPLAY (gtk_notebook_get_nth_page (priv->notebook, page));

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
  SysprofNotebookPrivate *priv = sysprof_notebook_get_instance_private (self);
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
  gtk_notebook_append_page (priv->notebook, GTK_WIDGET (replay), NULL);
  page = gtk_notebook_page_num (priv->notebook, GTK_WIDGET (replay));
  gtk_notebook_set_current_page (priv->notebook, page);
}

void
sysprof_notebook_add_profiler (SysprofNotebook *self,
                               SysprofProfiler *profiler)
{
  SysprofNotebookPrivate *priv = sysprof_notebook_get_instance_private (self);
  GtkWidget *display;
  gint page;

  g_return_if_fail (SYSPROF_IS_NOTEBOOK (self));
  g_return_if_fail (SYSPROF_IS_PROFILER (profiler));

  display = sysprof_display_new_for_profiler (profiler);

  gtk_widget_show (display);
  gtk_notebook_append_page (priv->notebook, GTK_WIDGET (display), NULL);
  page = gtk_notebook_page_num (priv->notebook, display);
  gtk_notebook_set_current_page (priv->notebook, page);
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
      gtk_notebook_set_show_tabs (priv->notebook,
                                  (priv->always_show_tabs ||
                                   gtk_notebook_get_n_pages (priv->notebook) > 1));
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_ALWAYS_SHOW_TABS]);
    }
}

static void
sysprof_notebook_add_child (GtkBuildable *buildable,
                            GtkBuilder   *builder,
                            GObject      *child,
                            const char   *type)
{
  SysprofNotebook *self = (SysprofNotebook *)buildable;
  SysprofNotebookPrivate *priv = sysprof_notebook_get_instance_private (self);

  g_assert (SYSPROF_IS_NOTEBOOK (self));

  if (SYSPROF_IS_DISPLAY (child))
    gtk_notebook_append_page (priv->notebook, GTK_WIDGET (child), NULL);
  else
    g_warning ("Cannot add child of type %s to %s",
               G_OBJECT_TYPE_NAME (child),
               G_OBJECT_TYPE_NAME (self));
}

static void
buildable_iface_init (GtkBuildableIface *iface)
{
  iface->add_child = sysprof_notebook_add_child;
}

guint
sysprof_notebook_get_n_pages (SysprofNotebook *self)
{
  SysprofNotebookPrivate *priv = sysprof_notebook_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_NOTEBOOK (self), 0);

  return gtk_notebook_get_n_pages (priv->notebook);
}

SysprofDisplay *
sysprof_notebook_get_nth_page (SysprofNotebook *self,
                               guint            nth)
{
  SysprofNotebookPrivate *priv = sysprof_notebook_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_NOTEBOOK (self), NULL);

  return SYSPROF_DISPLAY (gtk_notebook_get_nth_page (priv->notebook, nth));
}

void
sysprof_notebook_set_current_page (SysprofNotebook *self,
                                   int              nth)
{
  SysprofNotebookPrivate *priv = sysprof_notebook_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_NOTEBOOK (self));

  gtk_notebook_set_current_page (priv->notebook, nth);
}

int
sysprof_notebook_append (SysprofNotebook *self,
                         SysprofDisplay  *display)
{
  SysprofNotebookPrivate *priv = sysprof_notebook_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_NOTEBOOK (self), -1);
  g_return_val_if_fail (SYSPROF_IS_DISPLAY (display), -1);

  return gtk_notebook_append_page (priv->notebook, GTK_WIDGET (display), NULL);
}
