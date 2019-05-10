/* sysprof-profiler-menu-button.c
 *
 * Copyright 2016-2019 Christian Hergert <christian@hergert.me>
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

#include <glib/gi18n.h>
#include <string.h>
#include <sysprof.h>

#include "sysprof-model-filter.h"
#include "sysprof-process-model-row.h"
#include "sysprof-profiler-menu-button.h"

typedef struct
{
  SysprofProfiler           *profiler;
  SysprofModelFilter        *process_filter;

  /* Gtk template widgets */
  GtkTreeModel         *environment_model;
  GtkLabel             *label;
  GtkPopover           *popover;
  GtkEntry             *process_filter_entry;
  GtkListBox           *process_list_box;
  SysprofProcessModel       *process_model;
  GtkBox               *processes_box;
  GtkEntry             *spawn_entry;
  GtkStack             *stack;
  GtkSwitch            *whole_system_switch;
  GtkTreeView          *env_tree_view;
  GtkTreeViewColumn    *env_key_column;
  GtkTreeViewColumn    *env_value_column;
  GtkCellRenderer      *key_cell;
  GtkCellRenderer      *value_cell;
  GtkCheckButton       *inherit_environ;

  /* Property Bindings */
  GBinding             *inherit_binding;
  GBinding             *list_sensitive_binding;
  GBinding             *mutable_binding;
  GBinding             *whole_system_binding;

  /* Signal handlers */
  gulong                notify_whole_system_handler;

  /* GSources */
  guint                 save_env_source;
} SysprofProfilerMenuButtonPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (SysprofProfilerMenuButton, sysprof_profiler_menu_button, GTK_TYPE_MENU_BUTTON)

enum {
  PROP_0,
  PROP_PROFILER,
  N_PROPS
};

static void     sysprof_profiler_menu_button_env_row_changed (SysprofProfilerMenuButton *self,
                                                         GtkTreePath          *tree_path,
                                                         GtkTreeIter          *tree_iter,
                                                         gpointer              user_data);
static void     sysprof_profiler_menu_button_validate_spawn  (SysprofProfilerMenuButton *self,
                                                         GtkEntry             *entry);
static gboolean save_environ_to_gsettings               (gpointer              data);

static GParamSpec *properties [N_PROPS];

GtkWidget *
sysprof_profiler_menu_button_new (void)
{
  return g_object_new (SYSPROF_TYPE_PROFILER_MENU_BUTTON, NULL);
}

static void
sysprof_profiler_menu_button_update_label (SysprofProfilerMenuButton *self)
{
  SysprofProfilerMenuButtonPrivate *priv = sysprof_profiler_menu_button_get_instance_private (self);
  g_autofree gchar *str = NULL;
  const gchar *visible_child;
  const GPid *pids;
  guint n_pids = 0;

  g_assert (SYSPROF_IS_PROFILER_MENU_BUTTON (self));

  if (priv->profiler == NULL)
    {
      gtk_label_set_label (priv->label, "");
      return;
    }

  visible_child = gtk_stack_get_visible_child_name (priv->stack);

  if (g_strcmp0 (visible_child, "spawn") == 0)
    {
      const gchar *text;

      text = gtk_entry_get_text (priv->spawn_entry);

      if (text && *text)
        gtk_label_set_label (priv->label, text);
      else if (sysprof_profiler_get_whole_system (priv->profiler))
        gtk_label_set_label (priv->label, _("All Processes"));
      else
        gtk_label_set_label (priv->label, _("New Process"));

      sysprof_profiler_set_spawn (priv->profiler, text && *text);

      return;
    }

  sysprof_profiler_set_spawn (priv->profiler, FALSE);

  pids = sysprof_profiler_get_pids (priv->profiler, &n_pids);

  if (n_pids == 0 || sysprof_profiler_get_whole_system (priv->profiler))
    {
      gtk_label_set_label (priv->label, _("All Processes"));
      return;
    }

  if (n_pids == 1)
    {
      /* Translators: %d is the PID of the process. */
      str = g_strdup_printf (_("Process %d"), pids[0]);
      gtk_label_set_label (priv->label, str);
      return;
    }

  /* Translators: %u is the number (amount) of processes. */
  str = g_strdup_printf (ngettext("%u Process", "%u Processes", n_pids), n_pids);
  gtk_label_set_label (priv->label, str);
}

static void
clear_selected_flags (GtkWidget *widget,
                      gpointer   user_data)
{
  sysprof_process_model_row_set_selected (SYSPROF_PROCESS_MODEL_ROW (widget), FALSE);
}

static void
add_binding (GBinding      **binding,
             gpointer        src,
             const gchar    *src_property,
             gpointer        dst,
             const gchar    *dst_property,
             GBindingFlags   flags)
{
  g_assert (binding != NULL);
  g_assert (*binding == NULL);
  g_assert (src != NULL);
  g_assert (src_property != NULL);
  g_assert (dst != NULL);
  g_assert (dst_property != NULL);

  *binding = g_object_bind_property (src, src_property,
                                     dst, dst_property,
                                     flags);
  g_object_add_weak_pointer (G_OBJECT (*binding), (gpointer *)binding);
}

static void
clear_binding (GBinding **binding)
{
  g_assert (binding != NULL);
  g_assert (!*binding || G_IS_BINDING (*binding));

  if (*binding != NULL)
    {
      g_object_remove_weak_pointer (G_OBJECT (*binding), (gpointer *)binding);
      g_binding_unbind (*binding);
      *binding = NULL;
    }
}

static void
sysprof_profiler_menu_button_connect (SysprofProfilerMenuButton *self)
{
  SysprofProfilerMenuButtonPrivate *priv = sysprof_profiler_menu_button_get_instance_private (self);

  g_assert (SYSPROF_IS_PROFILER_MENU_BUTTON (self));
  g_assert (SYSPROF_IS_PROFILER (priv->profiler));

  add_binding (&priv->mutable_binding,
               priv->profiler, "is-mutable",
               self, "sensitive",
               G_BINDING_SYNC_CREATE);

  add_binding (&priv->whole_system_binding,
               priv->profiler, "whole-system",
               priv->whole_system_switch, "active",
               G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

  add_binding (&priv->list_sensitive_binding,
               priv->profiler, "whole-system",
               priv->processes_box, "visible",
               G_BINDING_SYNC_CREATE | G_BINDING_INVERT_BOOLEAN);

  add_binding (&priv->inherit_binding,
               priv->inherit_environ, "active",
               priv->profiler, "spawn-inherit-environ",
               G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

  priv->notify_whole_system_handler =
    g_signal_connect_object (priv->profiler,
                             "notify::whole-system",
                             G_CALLBACK (sysprof_profiler_menu_button_update_label),
                             self,
                             G_CONNECT_SWAPPED);

  sysprof_profiler_menu_button_update_label (self);

  sysprof_profiler_menu_button_validate_spawn (self, priv->spawn_entry);
  sysprof_profiler_menu_button_env_row_changed (self, NULL, NULL, NULL);
}

static void
sysprof_profiler_menu_button_disconnect (SysprofProfilerMenuButton *self)
{
  SysprofProfilerMenuButtonPrivate *priv = sysprof_profiler_menu_button_get_instance_private (self);

  g_assert (SYSPROF_IS_PROFILER_MENU_BUTTON (self));
  g_assert (SYSPROF_IS_PROFILER (priv->profiler));

  clear_binding (&priv->mutable_binding);
  clear_binding (&priv->whole_system_binding);
  clear_binding (&priv->list_sensitive_binding);
  clear_binding (&priv->inherit_binding);

  if (priv->save_env_source != 0)
    save_environ_to_gsettings (self);

  g_signal_handler_disconnect (priv->profiler, priv->notify_whole_system_handler);
  priv->notify_whole_system_handler = 0;

  gtk_widget_set_sensitive (GTK_WIDGET (self), FALSE);

  g_clear_object (&priv->profiler);

  gtk_container_foreach (GTK_CONTAINER (priv->process_list_box),
                         clear_selected_flags,
                         NULL);

  sysprof_profiler_menu_button_update_label (self);
}

/**
 * sysprof_profiler_menu_button_get_profiler:
 * @self: An #SysprofProfilerMenuButton
 *
 * Gets the profiler instance that is being configured.
 *
 * Returns: (nullable) (transfer none): An #SysprofProfiler or %NULL.
 */
SysprofProfiler *
sysprof_profiler_menu_button_get_profiler (SysprofProfilerMenuButton *self)
{
  SysprofProfilerMenuButtonPrivate *priv = sysprof_profiler_menu_button_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_PROFILER_MENU_BUTTON (self), NULL);

  return priv->profiler;
}

void
sysprof_profiler_menu_button_set_profiler (SysprofProfilerMenuButton *self,
                                      SysprofProfiler           *profiler)
{
  SysprofProfilerMenuButtonPrivate *priv = sysprof_profiler_menu_button_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_PROFILER_MENU_BUTTON (self));
  g_return_if_fail (!profiler || SYSPROF_IS_PROFILER (profiler));

  if (priv->profiler != profiler)
    {
      if (priv->profiler != NULL)
        sysprof_profiler_menu_button_disconnect (self);

      if (profiler != NULL)
        {
          priv->profiler = g_object_ref (profiler);
          sysprof_profiler_menu_button_connect (self);
        }

      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_PROFILER]);
    }
}

static void
sysprof_profiler_menu_button_row_activated (SysprofProfilerMenuButton *self,
                                       SysprofProcessModelRow    *row,
                                       GtkListBox           *list_box)
{
  SysprofProfilerMenuButtonPrivate *priv = sysprof_profiler_menu_button_get_instance_private (self);
  gboolean selected;

  g_assert (SYSPROF_IS_PROFILER_MENU_BUTTON (self));
  g_assert (SYSPROF_IS_PROCESS_MODEL_ROW (row));
  g_assert (GTK_IS_LIST_BOX (list_box));

  selected = !sysprof_process_model_row_get_selected (row);
  sysprof_process_model_row_set_selected (row, selected);

  if (priv->profiler != NULL)
    {
      SysprofProcessModelItem *item = sysprof_process_model_row_get_item (row);
      GPid pid = sysprof_process_model_item_get_pid (item);

      if (selected)
        sysprof_profiler_add_pid (priv->profiler, pid);
      else
        sysprof_profiler_remove_pid (priv->profiler, pid);
    }

  sysprof_profiler_menu_button_update_label (self);
}

static GtkWidget *
sysprof_profiler_menu_button_create_row (gpointer itemptr,
                                    gpointer user_data)
{
  SysprofProcessModelItem *item = itemptr;

  g_assert (SYSPROF_IS_PROCESS_MODEL_ITEM (item));
  g_assert (SYSPROF_IS_PROFILER_MENU_BUTTON (user_data));

  return g_object_new (SYSPROF_TYPE_PROCESS_MODEL_ROW,
                       "item", item,
                       "visible", TRUE,
                       NULL);
}

static void
sysprof_profiler_menu_button_clicked (GtkButton *button)
{
  SysprofProfilerMenuButton *self = (SysprofProfilerMenuButton *)button;
  SysprofProfilerMenuButtonPrivate *priv = sysprof_profiler_menu_button_get_instance_private (self);

  g_assert (SYSPROF_IS_PROFILER_MENU_BUTTON (self));

  sysprof_process_model_queue_reload (priv->process_model);

  GTK_BUTTON_CLASS (sysprof_profiler_menu_button_parent_class)->clicked (button);
}

static gboolean
sysprof_profiler_menu_button_filter_func (GObject  *object,
                                     gpointer  user_data)
{
  const gchar *needle = user_data;
  const gchar *haystack;

  g_assert (SYSPROF_IS_PROCESS_MODEL_ITEM (object));

  if (needle == NULL)
    return TRUE;

  haystack = sysprof_process_model_item_get_command_line (SYSPROF_PROCESS_MODEL_ITEM (object));

  if (haystack == NULL)
    return FALSE;

  return strstr (haystack, needle) != NULL;
}

static void
sysprof_profiler_menu_button_filter_changed (SysprofProfilerMenuButton *self,
                                        GtkEntry             *entry)
{
  SysprofProfilerMenuButtonPrivate *priv = sysprof_profiler_menu_button_get_instance_private (self);
  const gchar *text;

  g_assert (SYSPROF_IS_PROFILER_MENU_BUTTON (self));
  g_assert (GTK_IS_ENTRY (entry));

  text = gtk_entry_get_text (entry);
  if (text && *text == 0)
    text = NULL;

  sysprof_model_filter_set_filter_func (priv->process_filter,
                                   sysprof_profiler_menu_button_filter_func,
                                   g_strdup (text),
                                   g_free);
}

static void
sysprof_profiler_menu_button_constructed (GObject *object)
{
  SysprofProfilerMenuButton *self = (SysprofProfilerMenuButton *)object;
  SysprofProfilerMenuButtonPrivate *priv = sysprof_profiler_menu_button_get_instance_private (self);

  g_assert (SYSPROF_IS_PROFILER_MENU_BUTTON (self));

  priv->process_filter = sysprof_model_filter_new (G_LIST_MODEL (priv->process_model));

  gtk_list_box_bind_model (priv->process_list_box,
                           G_LIST_MODEL (priv->process_filter),
                           sysprof_profiler_menu_button_create_row,
                           self, NULL);

  G_OBJECT_CLASS (sysprof_profiler_menu_button_parent_class)->constructed (object);
}

static void
sysprof_profiler_menu_button_realize (GtkWidget *widget)
{
  SysprofProfilerMenuButton *self = (SysprofProfilerMenuButton *)widget;
  SysprofProfilerMenuButtonPrivate *priv = sysprof_profiler_menu_button_get_instance_private (self);
  g_autoptr(GSettings) settings = NULL;
  g_auto(GStrv) env = NULL;

  GTK_WIDGET_CLASS (sysprof_profiler_menu_button_parent_class)->realize (widget);

  settings = g_settings_new ("org.gnome.sysprof2");

  env = g_settings_get_strv (settings, "last-spawn-env");

  g_settings_bind (settings, "last-spawn-argv",
                   priv->spawn_entry, "text",
                   G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (settings, "last-spawn-inherit-env",
                   priv->inherit_environ, "active",
                   G_SETTINGS_BIND_DEFAULT);

  if (env)
    {
      GtkTreeModel *model;
      GtkTreeIter iter;
      guint i;

      model = gtk_tree_view_get_model (priv->env_tree_view);
      gtk_list_store_clear (GTK_LIST_STORE (model));

      for (i = 0; env [i]; i++)
        {
          const gchar *key = env [i];
          const gchar *value = NULL;
          gchar *eq = strchr (env[i], '=');

          if (eq)
            {
              *eq = '\0';
              value = eq + 1;
            }

          gtk_list_store_append (GTK_LIST_STORE (model), &iter);
          gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                              0, key,
                              1, value,
                              -1);
        }

      gtk_list_store_append (GTK_LIST_STORE (model), &iter);
    }
}

static gboolean
save_environ_to_gsettings (gpointer data)
{
  SysprofProfilerMenuButton *self = data;
  SysprofProfilerMenuButtonPrivate *priv = sysprof_profiler_menu_button_get_instance_private (self);
  g_autoptr(GPtrArray) ar = NULL;
  g_autoptr(GSettings) settings = NULL;
  GtkTreeIter iter;

  g_assert (SYSPROF_IS_PROFILER_MENU_BUTTON (self));

  priv->save_env_source = 0;

  if (priv->environment_model == NULL)
    return G_SOURCE_REMOVE;

  settings = g_settings_new ("org.gnome.sysprof2");

  ar = g_ptr_array_new_with_free_func (g_free);

  if (gtk_tree_model_get_iter_first (priv->environment_model, &iter))
    {
      do
        {
          g_autofree gchar *key = NULL;
          g_autofree gchar *value = NULL;

          gtk_tree_model_get (priv->environment_model, &iter,
                              0, &key,
                              1, &value,
                              -1);

          if (!key || !*key)
            continue;

          g_ptr_array_add (ar, g_strdup_printf ("%s=%s", key, value ? value : ""));
        }
      while (gtk_tree_model_iter_next (priv->environment_model, &iter));
    }

  g_ptr_array_add (ar, NULL);

  g_settings_set_strv (settings, "last-spawn-env", (const gchar * const *)ar->pdata);

  return G_SOURCE_REMOVE;
}


static void
sysprof_profiler_menu_button_destroy (GtkWidget *widget)
{
  SysprofProfilerMenuButton *self = (SysprofProfilerMenuButton *)widget;
  SysprofProfilerMenuButtonPrivate *priv = sysprof_profiler_menu_button_get_instance_private (self);

  if (priv->profiler != NULL)
    {
      sysprof_profiler_menu_button_disconnect (self);
      g_clear_object (&priv->profiler);
    }

  g_clear_object (&priv->process_filter);

  if (priv->save_env_source)
    {
      g_source_remove (priv->save_env_source);
      priv->save_env_source = 0;
    }

  GTK_WIDGET_CLASS (sysprof_profiler_menu_button_parent_class)->destroy (widget);
}

static void
sysprof_profiler_menu_button_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  SysprofProfilerMenuButton *self = SYSPROF_PROFILER_MENU_BUTTON (object);

  switch (prop_id)
    {
    case PROP_PROFILER:
      g_value_set_object (value, sysprof_profiler_menu_button_get_profiler (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_profiler_menu_button_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  SysprofProfilerMenuButton *self = SYSPROF_PROFILER_MENU_BUTTON (object);

  switch (prop_id)
    {
    case PROP_PROFILER:
      sysprof_profiler_menu_button_set_profiler (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_profiler_menu_button_class_init (SysprofProfilerMenuButtonClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkButtonClass *button_class = GTK_BUTTON_CLASS (klass);

  object_class->constructed = sysprof_profiler_menu_button_constructed;
  object_class->get_property = sysprof_profiler_menu_button_get_property;
  object_class->set_property = sysprof_profiler_menu_button_set_property;

  widget_class->destroy = sysprof_profiler_menu_button_destroy;
  widget_class->realize = sysprof_profiler_menu_button_realize;

  button_class->clicked = sysprof_profiler_menu_button_clicked;

  properties [PROP_PROFILER] =
    g_param_spec_object ("profiler",
                         "Profiler",
                         "Profiler",
                         SYSPROF_TYPE_PROFILER,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/ui/sysprof-profiler-menu-button.ui");
  gtk_widget_class_bind_template_child_private (widget_class, SysprofProfilerMenuButton, env_key_column);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofProfilerMenuButton, env_tree_view);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofProfilerMenuButton, env_value_column);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofProfilerMenuButton, environment_model);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofProfilerMenuButton, inherit_environ);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofProfilerMenuButton, key_cell);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofProfilerMenuButton, label);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofProfilerMenuButton, popover);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofProfilerMenuButton, process_filter_entry);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofProfilerMenuButton, process_list_box);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofProfilerMenuButton, process_model);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofProfilerMenuButton, processes_box);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofProfilerMenuButton, spawn_entry);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofProfilerMenuButton, stack);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofProfilerMenuButton, value_cell);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofProfilerMenuButton, whole_system_switch);
}

static void
sysprof_profiler_menu_button_env_row_changed (SysprofProfilerMenuButton *self,
                                         GtkTreePath          *tree_path,
                                         GtkTreeIter          *tree_iter,
                                         gpointer              user_data)
{
  SysprofProfilerMenuButtonPrivate *priv = sysprof_profiler_menu_button_get_instance_private (self);
  g_autoptr(GPtrArray) env = NULL;
  GtkTreeModel *model;
  GtkTreeIter iter;

  g_assert (SYSPROF_IS_PROFILER_MENU_BUTTON (self));
  g_assert (GTK_IS_TREE_MODEL (priv->environment_model));

  /* queue saving settings to gsettings */
  if (priv->save_env_source)
    g_source_remove (priv->save_env_source);
  priv->save_env_source = g_timeout_add_seconds_full (G_PRIORITY_DEFAULT,
                                                      1,
                                                      save_environ_to_gsettings,
                                                      g_object_ref (self),
                                                      g_object_unref);

  if (priv->profiler == NULL)
    return;

  /* sync the environ to the profiler */
  env = g_ptr_array_new_with_free_func (g_free);
  model = priv->environment_model;

  if (gtk_tree_model_get_iter_first (model, &iter))
    {
      do
        {
          g_autofree gchar *key = NULL;
          g_autofree gchar *value = NULL;

          gtk_tree_model_get (model, &iter,
                              0, &key,
                              1, &value,
                              -1);

          if (key && *key)
            g_ptr_array_add (env, g_strdup_printf ("%s=%s", key, value));
        }
      while (gtk_tree_model_iter_next (model, &iter));
    }
  g_ptr_array_add (env, NULL);
  sysprof_profiler_set_spawn_env (priv->profiler, (const gchar * const *)env->pdata);
}

static void
on_backspace (SysprofProfilerMenuButton *self,
              GtkEntry             *entry)
{
  SysprofProfilerMenuButtonPrivate *priv = sysprof_profiler_menu_button_get_instance_private (self);

  if (g_object_get_data (G_OBJECT (entry), "CELL_WAS_EMPTY"))
    {
      GtkTreeModel *model;
      GtkTreeSelection *selection;
      GtkTreeIter iter;

      model = gtk_tree_view_get_model (priv->env_tree_view);
      selection = gtk_tree_view_get_selection (priv->env_tree_view);

      if (gtk_tree_selection_get_selected (selection, NULL, &iter))
        {
          gtk_cell_renderer_stop_editing (priv->key_cell, TRUE);
          gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
        }
    }
  else
    g_object_set_data (G_OBJECT (entry), "CELL_WAS_EMPTY",
                       GINT_TO_POINTER (*gtk_entry_get_text (entry) == '\0'));
}

static void
sysprof_profiler_menu_button_env_key_editing_started (SysprofProfilerMenuButton *self,
                                                 GtkCellEditable      *editable,
                                                 const gchar          *path,
                                                 GtkCellRenderer      *cell)
{
  g_signal_connect_object (editable,
                           "backspace",
                           G_CALLBACK (on_backspace),
                           self,
                           G_CONNECT_AFTER | G_CONNECT_SWAPPED);
}

static void
sysprof_profiler_menu_button_env_key_edited (SysprofProfilerMenuButton *self,
                                        const gchar          *path,
                                        const gchar          *new_text,
                                        GtkCellRendererText  *cell)
{
  SysprofProfilerMenuButtonPrivate *priv = sysprof_profiler_menu_button_get_instance_private (self);
  GtkTreeModel *model;
  GtkTreePath *tree_path;
  GtkTreeIter iter;

  g_assert (SYSPROF_IS_PROFILER_MENU_BUTTON (self));
  g_assert (path != NULL);
  g_assert (new_text != NULL);
  g_assert (GTK_IS_CELL_RENDERER_TEXT (cell));

  model = gtk_tree_view_get_model (priv->env_tree_view);

  tree_path = gtk_tree_path_new_from_string (path);

  if (gtk_tree_model_get_iter (model, &iter, tree_path))
    {
      gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                          0, new_text,
                          -1);

      if (!gtk_tree_model_iter_next (model, &iter))
        gtk_list_store_append (GTK_LIST_STORE (model), &iter);

      gtk_tree_view_set_cursor_on_cell (priv->env_tree_view,
                                        tree_path,
                                        priv->env_value_column,
                                        priv->value_cell,
                                        TRUE);
    }

  gtk_tree_path_free (tree_path);
}

static void
sysprof_profiler_menu_button_env_value_edited (SysprofProfilerMenuButton *self,
                                          const gchar          *path,
                                          const gchar          *new_text,
                                          GtkCellRendererText  *cell)
{
  SysprofProfilerMenuButtonPrivate *priv = sysprof_profiler_menu_button_get_instance_private (self);

  GtkTreeModel *model;
  GtkTreePath *tree_path;
  GtkTreeIter iter;

  g_assert (SYSPROF_IS_PROFILER_MENU_BUTTON (self));
  g_assert (path != NULL);
  g_assert (new_text != NULL);
  g_assert (GTK_IS_CELL_RENDERER_TEXT (cell));

  model = gtk_tree_view_get_model (priv->env_tree_view);

  tree_path = gtk_tree_path_new_from_string (path);

  if (gtk_tree_model_get_iter (model, &iter, tree_path))
    {
      gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                          1, new_text,
                          -1);

      if (!gtk_tree_model_iter_next (model, &iter))
        gtk_list_store_append (GTK_LIST_STORE (model), &iter);

      gtk_tree_path_next (tree_path);

      gtk_tree_view_set_cursor_on_cell (priv->env_tree_view,
                                        tree_path,
                                        priv->env_key_column,
                                        priv->key_cell,
                                        TRUE);
    }

  gtk_tree_path_free (tree_path);
}

static void
sysprof_profiler_menu_button_validate_spawn (SysprofProfilerMenuButton *self,
                                        GtkEntry             *entry)
{
  SysprofProfilerMenuButtonPrivate *priv = sysprof_profiler_menu_button_get_instance_private (self);
  g_auto(GStrv) argv = NULL;
  g_autoptr(GError) error = NULL;
  const gchar *text;
  gint argc;

  g_assert (SYSPROF_IS_PROFILER_MENU_BUTTON (self));
  g_assert (GTK_IS_ENTRY (entry));

  text = gtk_entry_get_text (entry);

  if (text && *text && !g_shell_parse_argv (text, &argc, &argv, &error))
    {
      sysprof_profiler_set_spawn_argv (priv->profiler, NULL);
      g_object_set (entry,
                    "secondary-icon-name", "dialog-warning-symbolic",
                    "secondary-icon-tooltip-text", _("The command line arguments provided are invalid"),
                    NULL);
    }
  else
    {
      g_autoptr(GPtrArray) cooked = g_ptr_array_new ();

      if (argv != NULL)
        {
          if (g_file_test ("/.flatpak-info", G_FILE_TEST_EXISTS))
            {
              g_ptr_array_add (cooked, "flatpak-spawn");
              g_ptr_array_add (cooked, "--host");
              g_ptr_array_add (cooked, "--watch-bus");
            }

          for (guint i = 0; argv[i] != NULL; i++)
            g_ptr_array_add (cooked, argv[i]);
        }

      g_ptr_array_add (cooked, NULL);

      sysprof_profiler_set_spawn_argv (priv->profiler,
                                       (const gchar * const *)cooked->pdata);

      g_object_set (entry,
                    "secondary-icon-name", NULL,
                    "secondary-icon-tooltip-text", NULL,
                    NULL);
    }
}

static void
sysprof_profiler_menu_button_init (SysprofProfilerMenuButton *self)
{
  SysprofProfilerMenuButtonPrivate *priv = sysprof_profiler_menu_button_get_instance_private (self);

  gtk_widget_init_template (GTK_WIDGET (self));

  g_signal_connect_object (priv->process_filter_entry,
                           "changed",
                           G_CALLBACK (sysprof_profiler_menu_button_filter_changed),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (priv->spawn_entry,
                           "changed",
                           G_CALLBACK (sysprof_profiler_menu_button_update_label),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (priv->spawn_entry,
                           "changed",
                           G_CALLBACK (sysprof_profiler_menu_button_validate_spawn),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (priv->stack,
                           "notify::visible-child",
                           G_CALLBACK (sysprof_profiler_menu_button_update_label),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (priv->process_list_box,
                           "row-activated",
                           G_CALLBACK (sysprof_profiler_menu_button_row_activated),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (priv->key_cell,
                           "edited",
                           G_CALLBACK (sysprof_profiler_menu_button_env_key_edited),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (priv->value_cell,
                           "edited",
                           G_CALLBACK (sysprof_profiler_menu_button_env_value_edited),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (gtk_tree_view_get_model (priv->env_tree_view),
                           "row-changed",
                           G_CALLBACK (sysprof_profiler_menu_button_env_row_changed),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (priv->key_cell,
                           "editing-started",
                           G_CALLBACK (sysprof_profiler_menu_button_env_key_editing_started),
                           self,
                           G_CONNECT_SWAPPED);

  gtk_widget_set_sensitive (GTK_WIDGET (self), FALSE);
}
