/* sysprof-process-model-row.c
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

#define G_LOG_DOMAIN "sysprof-process-model-row"

#include "config.h"

#include "sysprof-process-model-row.h"

typedef struct
{
  SysprofProcessModelItem *item;

  GtkLabel *args_label;
  GtkLabel *label;
  GtkLabel *pid;
  GtkImage *image;
  GtkImage *check;
} SysprofProcessModelRowPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (SysprofProcessModelRow, sysprof_process_model_row, GTK_TYPE_LIST_BOX_ROW)

enum {
  PROP_0,
  PROP_ITEM,
  PROP_SELECTED,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

GtkWidget *
sysprof_process_model_row_new (SysprofProcessModelItem *item)
{
  return g_object_new (SYSPROF_TYPE_PROCESS_MODEL_ROW,
                       "item", item,
                       NULL);
}

SysprofProcessModelItem *
sysprof_process_model_row_get_item (SysprofProcessModelRow *self)
{
  SysprofProcessModelRowPrivate *priv = sysprof_process_model_row_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_PROCESS_MODEL_ROW (self), NULL);

  return priv->item;
}

static void
sysprof_process_model_row_set_item (SysprofProcessModelRow  *self,
                               SysprofProcessModelItem *item)
{
  SysprofProcessModelRowPrivate *priv = sysprof_process_model_row_get_instance_private (self);

  g_assert (SYSPROF_IS_PROCESS_MODEL_ROW (self));
  g_assert (SYSPROF_IS_PROCESS_MODEL_ITEM (item));

  if (g_set_object (&priv->item, item))
    {
      const gchar *command_line;
      g_auto(GStrv) parts = NULL;
      g_autofree gchar *pidstr = NULL;
      const gchar * const *argv;
      GPid pid;

      command_line = sysprof_process_model_item_get_command_line (item);
      parts = g_strsplit (command_line ?: "", "\n", 0);
      gtk_label_set_label (priv->label, parts [0]);

      if ((NULL != (argv = sysprof_process_model_item_get_argv (item))) && (argv[0] != NULL))
        {
          g_autofree gchar *argvstr = g_strjoinv (" ", (gchar **)&argv[1]);
          g_autofree gchar *escaped = g_markup_escape_text (argvstr, -1);

          gtk_label_set_label (priv->args_label, escaped);
        }

      pid = sysprof_process_model_item_get_pid (item);
      pidstr = g_strdup_printf ("<small>%u</small>", pid);
      gtk_label_set_label (priv->pid, pidstr);
      gtk_label_set_use_markup (priv->pid, TRUE);
    }
}

gboolean
sysprof_process_model_row_get_selected (SysprofProcessModelRow *self)
{
  SysprofProcessModelRowPrivate *priv = sysprof_process_model_row_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_PROCESS_MODEL_ROW (self), FALSE);

  return gtk_widget_get_visible (GTK_WIDGET (priv->check));
}

void
sysprof_process_model_row_set_selected (SysprofProcessModelRow *self,
                                   gboolean           selected)
{
  SysprofProcessModelRowPrivate *priv = sysprof_process_model_row_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_PROCESS_MODEL_ROW (self));

  selected = !!selected;

  if (selected != sysprof_process_model_row_get_selected (self))
    {
      gtk_widget_set_visible (GTK_WIDGET (priv->check), selected);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_SELECTED]);
    }
}

static gboolean
sysprof_process_model_row_query_tooltip (GtkWidget  *widget,
                                    gint        x,
                                    gint        y,
                                    gboolean    keyboard_mode,
                                    GtkTooltip *tooltip)
{
  SysprofProcessModelRow *self = (SysprofProcessModelRow *)widget;
  SysprofProcessModelRowPrivate *priv = sysprof_process_model_row_get_instance_private (self);

  g_assert (SYSPROF_IS_PROCESS_MODEL_ROW (self));
  g_assert (GTK_IS_TOOLTIP (tooltip));

  if (priv->item != NULL)
    {
      const gchar * const *argv = sysprof_process_model_item_get_argv (priv->item);

      if (argv != NULL)
        {
          g_autofree gchar *str = g_strjoinv (" ", (gchar **)argv);
          gtk_tooltip_set_text (tooltip, str);
          return TRUE;
        }
    }

  return FALSE;
}

static void
sysprof_process_model_row_finalize (GObject *object)
{
  SysprofProcessModelRow *self = (SysprofProcessModelRow *)object;
  SysprofProcessModelRowPrivate *priv = sysprof_process_model_row_get_instance_private (self);

  g_clear_object (&priv->item);

  G_OBJECT_CLASS (sysprof_process_model_row_parent_class)->finalize (object);
}

static void
sysprof_process_model_row_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  SysprofProcessModelRow *self = SYSPROF_PROCESS_MODEL_ROW (object);

  switch (prop_id)
    {
    case PROP_ITEM:
      g_value_set_object (value, sysprof_process_model_row_get_item (self));
      break;

    case PROP_SELECTED:
      g_value_set_boolean (value, sysprof_process_model_row_get_selected (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_process_model_row_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  SysprofProcessModelRow *self = SYSPROF_PROCESS_MODEL_ROW (object);

  switch (prop_id)
    {
    case PROP_ITEM:
      sysprof_process_model_row_set_item (self, g_value_get_object (value));
      break;

    case PROP_SELECTED:
      sysprof_process_model_row_set_selected (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_process_model_row_class_init (SysprofProcessModelRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = sysprof_process_model_row_finalize;
  object_class->get_property = sysprof_process_model_row_get_property;
  object_class->set_property = sysprof_process_model_row_set_property;

  widget_class->query_tooltip = sysprof_process_model_row_query_tooltip;

  properties [PROP_ITEM] =
    g_param_spec_object ("item",
                         "Item",
                         "Item",
                         SYSPROF_TYPE_PROCESS_MODEL_ITEM,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SELECTED] =
    g_param_spec_boolean ("selected",
                          "Selected",
                          "Selected",
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/sysprof/ui/sysprof-process-model-row.ui");
  gtk_widget_class_bind_template_child_private (widget_class, SysprofProcessModelRow, args_label);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofProcessModelRow, image);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofProcessModelRow, label);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofProcessModelRow, pid);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofProcessModelRow, check);
}

static void
sysprof_process_model_row_init (SysprofProcessModelRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_widget_set_has_tooltip (GTK_WIDGET (self), TRUE);
}
