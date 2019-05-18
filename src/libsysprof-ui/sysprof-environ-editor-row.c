/* sysprof-environ-editor-row.c
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

#define G_LOG_DOMAIN "sysprof-environ-editor-row"

#include "config.h"

#include "sysprof-environ-editor-row.h"

struct _SysprofEnvironEditorRow
{
  GtkListBoxRow           parent_instance;

  SysprofEnvironVariable *variable;

  GtkEntry               *key_entry;
  GtkEntry               *value_entry;
  GtkButton              *delete_button;

  GBinding               *key_binding;
  GBinding               *value_binding;
};

enum {
  PROP_0,
  PROP_VARIABLE,
  LAST_PROP
};

enum {
  DELETE,
  LAST_SIGNAL
};

G_DEFINE_TYPE (SysprofEnvironEditorRow, sysprof_environ_editor_row, GTK_TYPE_LIST_BOX_ROW)

static GParamSpec *properties [LAST_PROP];
static guint signals [LAST_SIGNAL];

static gboolean
null_safe_mapping (GBinding     *binding,
                   const GValue *from_value,
                   GValue       *to_value,
                   gpointer      user_data)
{
  const gchar *str = g_value_get_string (from_value);
  g_value_set_string (to_value, str ?: "");
  return TRUE;
}

static void
sysprof_environ_editor_row_connect (SysprofEnvironEditorRow *self)
{
  g_assert (SYSPROF_IS_ENVIRON_EDITOR_ROW (self));
  g_assert (SYSPROF_IS_ENVIRON_VARIABLE (self->variable));

  self->key_binding =
    g_object_bind_property_full (self->variable, "key", self->key_entry, "text",
                                 G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL,
                                 null_safe_mapping, NULL, NULL, NULL);

  self->value_binding =
    g_object_bind_property_full (self->variable, "value", self->value_entry, "text",
                                 G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL,
                                 null_safe_mapping, NULL, NULL, NULL);
}

static void
sysprof_environ_editor_row_disconnect (SysprofEnvironEditorRow *self)
{
  g_assert (SYSPROF_IS_ENVIRON_EDITOR_ROW (self));
  g_assert (SYSPROF_IS_ENVIRON_VARIABLE (self->variable));

  g_clear_pointer (&self->key_binding, g_binding_unbind);
  g_clear_pointer (&self->value_binding, g_binding_unbind);
}

static void
delete_button_clicked (GtkButton               *button,
                       SysprofEnvironEditorRow *self)
{
  g_assert (GTK_IS_BUTTON (button));
  g_assert (SYSPROF_IS_ENVIRON_EDITOR_ROW (self));

  g_signal_emit (self, signals [DELETE], 0);
}

static void
key_entry_activate (GtkWidget               *entry,
                    SysprofEnvironEditorRow *self)
{
  g_assert (GTK_IS_ENTRY (entry));
  g_assert (SYSPROF_IS_ENVIRON_EDITOR_ROW (self));

  gtk_widget_grab_focus (GTK_WIDGET (self->value_entry));
}

static void
value_entry_activate (GtkWidget               *entry,
                      SysprofEnvironEditorRow *self)
{
  GtkWidget *parent;

  g_assert (GTK_IS_ENTRY (entry));
  g_assert (SYSPROF_IS_ENVIRON_EDITOR_ROW (self));

  gtk_widget_grab_focus (GTK_WIDGET (self));
  parent = gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_LIST_BOX);
  g_signal_emit_by_name (parent, "move-cursor", GTK_MOVEMENT_DISPLAY_LINES, 1);
}

static void
sysprof_environ_editor_row_destroy (GtkWidget *widget)
{
  SysprofEnvironEditorRow *self = (SysprofEnvironEditorRow *)widget;

  if (self->variable != NULL)
    {
      sysprof_environ_editor_row_disconnect (self);
      g_clear_object (&self->variable);
    }

  GTK_WIDGET_CLASS (sysprof_environ_editor_row_parent_class)->destroy (widget);
}

static void
sysprof_environ_editor_row_get_property (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
  SysprofEnvironEditorRow *self = SYSPROF_ENVIRON_EDITOR_ROW (object);

  switch (prop_id)
    {
    case PROP_VARIABLE:
      g_value_set_object (value, sysprof_environ_editor_row_get_variable (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_environ_editor_row_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
  SysprofEnvironEditorRow *self = SYSPROF_ENVIRON_EDITOR_ROW (object);

  switch (prop_id)
    {
    case PROP_VARIABLE:
      sysprof_environ_editor_row_set_variable (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_environ_editor_row_class_init (SysprofEnvironEditorRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = sysprof_environ_editor_row_get_property;
  object_class->set_property = sysprof_environ_editor_row_set_property;

  widget_class->destroy = sysprof_environ_editor_row_destroy;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/ui/sysprof-environ-editor-row.ui");
  gtk_widget_class_bind_template_child (widget_class, SysprofEnvironEditorRow, delete_button);
  gtk_widget_class_bind_template_child (widget_class, SysprofEnvironEditorRow, key_entry);
  gtk_widget_class_bind_template_child (widget_class, SysprofEnvironEditorRow, value_entry);

  properties [PROP_VARIABLE] =
    g_param_spec_object ("variable",
                         "Variable",
                         "Variable",
                         SYSPROF_TYPE_ENVIRON_VARIABLE,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, LAST_PROP, properties);

  signals [DELETE] =
    g_signal_new ("delete",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
sysprof_environ_editor_row_init (SysprofEnvironEditorRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  g_signal_connect (self->delete_button,
                    "clicked",
                    G_CALLBACK (delete_button_clicked),
                    self);

  g_signal_connect (self->key_entry,
                    "activate",
                    G_CALLBACK (key_entry_activate),
                    self);

  g_signal_connect (self->value_entry,
                    "activate",
                    G_CALLBACK (value_entry_activate),
                    self);
}

/**
 * sysprof_environ_editor_row_get_variable:
 *
 * Returns: (transfer none) (nullable): An #SysprofEnvironVariable.
 */
SysprofEnvironVariable *
sysprof_environ_editor_row_get_variable (SysprofEnvironEditorRow *self)
{
  g_return_val_if_fail (SYSPROF_IS_ENVIRON_EDITOR_ROW (self), NULL);

  return self->variable;
}

void
sysprof_environ_editor_row_set_variable (SysprofEnvironEditorRow *self,
                                         SysprofEnvironVariable  *variable)
{
  g_return_if_fail (SYSPROF_IS_ENVIRON_EDITOR_ROW (self));
  g_return_if_fail (!variable || SYSPROF_IS_ENVIRON_VARIABLE (variable));

  if (variable != self->variable)
    {
      if (self->variable != NULL)
        {
          sysprof_environ_editor_row_disconnect (self);
          g_clear_object (&self->variable);
        }

      if (variable != NULL)
        {
          self->variable = g_object_ref (variable);
          sysprof_environ_editor_row_connect (self);
        }

      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_VARIABLE]);
    }
}

void
sysprof_environ_editor_row_start_editing (SysprofEnvironEditorRow *self)
{
  g_return_if_fail (SYSPROF_IS_ENVIRON_EDITOR_ROW (self));

  gtk_widget_grab_focus (GTK_WIDGET (self->key_entry));
}
