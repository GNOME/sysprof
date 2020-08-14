/* sysprof-environ-editor.c
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

#define G_LOG_DOMAIN "sysprof-environ-editor"

#include "config.h"

#include <glib/gi18n.h>

#include "sysprof-environ-editor.h"
#include "sysprof-environ-editor-row.h"
#include "sysprof-theme-manager.h"

struct _SysprofEnvironEditor
{
  GtkListBox              parent_instance;
  SysprofEnviron         *environ;
  GtkWidget              *dummy_row;
  SysprofEnvironVariable *dummy;
};

G_DEFINE_TYPE (SysprofEnvironEditor, sysprof_environ_editor, GTK_TYPE_LIST_BOX)

enum {
  PROP_0,
  PROP_ENVIRON,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

static void
sysprof_environ_editor_delete_row (SysprofEnvironEditor    *self,
                                   SysprofEnvironEditorRow *row)
{
  SysprofEnvironVariable *variable;

  g_assert (SYSPROF_IS_ENVIRON_EDITOR (self));
  g_assert (SYSPROF_IS_ENVIRON_EDITOR_ROW (row));

  variable = sysprof_environ_editor_row_get_variable (row);
  sysprof_environ_remove (self->environ, variable);
}

static GtkWidget *
sysprof_environ_editor_create_dummy_row (SysprofEnvironEditor *self)
{
  GtkWidget *row;
  GtkWidget *label;

  g_assert (SYSPROF_IS_ENVIRON_EDITOR (self));

  label = g_object_new (GTK_TYPE_LABEL,
                        "label", _("New variable…"),
                        "margin", 6,
                        "visible", TRUE,
                        "xalign", 0.0f,
                        NULL);
  gtk_style_context_add_class (gtk_widget_get_style_context (label), "dim-label");

  row = g_object_new (GTK_TYPE_LIST_BOX_ROW,
                      "child", label,
                      "visible", TRUE,
                      NULL);

  return row;
}

static GtkWidget *
sysprof_environ_editor_create_row (gpointer item,
                                   gpointer user_data)
{
  SysprofEnvironVariable *variable = item;
  SysprofEnvironEditor *self = user_data;
  SysprofEnvironEditorRow *row;

  g_assert (SYSPROF_IS_ENVIRON_EDITOR (self));
  g_assert (SYSPROF_IS_ENVIRON_VARIABLE (variable));

  row = g_object_new (SYSPROF_TYPE_ENVIRON_EDITOR_ROW,
                      "variable", variable,
                      "visible", TRUE,
                      NULL);

  g_signal_connect_object (row,
                           "delete",
                           G_CALLBACK (sysprof_environ_editor_delete_row),
                           self,
                           G_CONNECT_SWAPPED);

  return GTK_WIDGET (row);
}

static void
sysprof_environ_editor_disconnect (SysprofEnvironEditor *self)
{
  g_assert (SYSPROF_IS_ENVIRON_EDITOR (self));
  g_assert (SYSPROF_IS_ENVIRON (self->environ));

  gtk_list_box_bind_model (GTK_LIST_BOX (self), NULL, NULL, NULL, NULL);

  g_clear_object (&self->dummy);
}

static void
sysprof_environ_editor_connect (SysprofEnvironEditor *self)
{
  g_assert (SYSPROF_IS_ENVIRON_EDITOR (self));
  g_assert (SYSPROF_IS_ENVIRON (self->environ));

  gtk_list_box_bind_model (GTK_LIST_BOX (self),
                           G_LIST_MODEL (self->environ),
                           sysprof_environ_editor_create_row, self, NULL);

  self->dummy_row = sysprof_environ_editor_create_dummy_row (self);
  gtk_container_add (GTK_CONTAINER (self), self->dummy_row);
}

static void
find_row_cb (GtkWidget *widget,
             gpointer   data)
{
  struct {
    SysprofEnvironVariable  *variable;
    SysprofEnvironEditorRow *row;
  } *lookup = data;

  g_assert (lookup != NULL);
  g_assert (GTK_IS_LIST_BOX_ROW (widget));

  if (SYSPROF_IS_ENVIRON_EDITOR_ROW (widget))
    {
      SysprofEnvironVariable *variable;

      variable = sysprof_environ_editor_row_get_variable (SYSPROF_ENVIRON_EDITOR_ROW (widget));

      if (variable == lookup->variable)
        lookup->row = SYSPROF_ENVIRON_EDITOR_ROW (widget);
    }
}

static SysprofEnvironEditorRow *
find_row (SysprofEnvironEditor   *self,
          SysprofEnvironVariable *variable)
{
  struct {
    SysprofEnvironVariable  *variable;
    SysprofEnvironEditorRow *row;
  } lookup = { variable, NULL };

  g_assert (SYSPROF_IS_ENVIRON_EDITOR (self));
  g_assert (SYSPROF_IS_ENVIRON_VARIABLE (variable));

  gtk_container_foreach (GTK_CONTAINER (self), find_row_cb, &lookup);

  return lookup.row;
}

static void
sysprof_environ_editor_row_activated (GtkListBox    *list_box,
                                      GtkListBoxRow *row)
{
  SysprofEnvironEditor *self = (SysprofEnvironEditor *)list_box;

  g_assert (GTK_IS_LIST_BOX (list_box));
  g_assert (GTK_IS_LIST_BOX_ROW (row));

  if (self->environ == NULL)
    return;

  if (self->dummy_row == GTK_WIDGET (row))
    {
      g_autoptr(SysprofEnvironVariable) variable = NULL;

      variable = sysprof_environ_variable_new (NULL, NULL);
      sysprof_environ_append (self->environ, variable);
      sysprof_environ_editor_row_start_editing (find_row (self, variable));
    }
}

static void
sysprof_environ_editor_destroy (GtkWidget *widget)
{
  SysprofEnvironEditor *self = (SysprofEnvironEditor *)widget;

  GTK_WIDGET_CLASS (sysprof_environ_editor_parent_class)->destroy (widget);

  g_clear_object (&self->environ);
}

static void
sysprof_environ_editor_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  SysprofEnvironEditor *self = SYSPROF_ENVIRON_EDITOR (object);

  switch (prop_id)
    {
    case PROP_ENVIRON:
      g_value_set_object (value, sysprof_environ_editor_get_environ (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

static void
sysprof_environ_editor_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  SysprofEnvironEditor *self = SYSPROF_ENVIRON_EDITOR (object);

  switch (prop_id)
    {
    case PROP_ENVIRON:
      sysprof_environ_editor_set_environ (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

static void
sysprof_environ_editor_class_init (SysprofEnvironEditorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkListBoxClass *list_box_class = GTK_LIST_BOX_CLASS (klass);
  SysprofThemeManager *theme_manager = sysprof_theme_manager_get_default ();

  object_class->get_property = sysprof_environ_editor_get_property;
  object_class->set_property = sysprof_environ_editor_set_property;

  widget_class->destroy = sysprof_environ_editor_destroy;

  list_box_class->row_activated = sysprof_environ_editor_row_activated;

  properties [PROP_ENVIRON] =
    g_param_spec_object ("environ",
                         "Environment",
                         "Environment",
                         SYSPROF_TYPE_ENVIRON,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  sysprof_theme_manager_register_resource (theme_manager, NULL, NULL, "/org/gnome/sysprof/css/SysprofEnvironEditor-shared.css");
}

static void
sysprof_environ_editor_init (SysprofEnvironEditor *self)
{
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (self), GTK_SELECTION_NONE);

  gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (self)),
                               "environ-editor");
}

GtkWidget *
sysprof_environ_editor_new (void)
{
  return g_object_new (SYSPROF_TYPE_ENVIRON_EDITOR, NULL);
}

void
sysprof_environ_editor_set_environ (SysprofEnvironEditor *self,
                                    SysprofEnviron       *environ_)
{
  g_return_if_fail (SYSPROF_IS_ENVIRON_EDITOR (self));
  g_return_if_fail (SYSPROF_IS_ENVIRON (environ_));

  if (self->environ != environ_)
    {
      if (self->environ != NULL)
        {
          sysprof_environ_editor_disconnect (self);
          g_clear_object (&self->environ);
        }

      if (environ_ != NULL)
        {
          self->environ = g_object_ref (environ_);
          sysprof_environ_editor_connect (self);
        }

      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_ENVIRON]);
    }
}

/**
 * sysprof_environ_editor_get_environ:
 *
 * Returns: (nullable) (transfer none): An #SysprofEnviron or %NULL.
 *
 * Since: 3.34
 */
SysprofEnviron *
sysprof_environ_editor_get_environ (SysprofEnvironEditor *self)
{
  g_return_val_if_fail (SYSPROF_IS_ENVIRON_EDITOR (self), NULL);

  return self->environ;
}
