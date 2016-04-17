/* sp-process-model-row.c
 *
 * Copyright (C) 2016 Christian Hergert <christian@hergert.me>
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
 */

#include "sp-process-model-row.h"

typedef struct
{
  SpProcessModelItem *item;

  GtkLabel *args_label;
  GtkLabel *label;
  GtkLabel *pid;
  GtkImage *image;
  GtkImage *check;
} SpProcessModelRowPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (SpProcessModelRow, sp_process_model_row, GTK_TYPE_LIST_BOX_ROW)

enum {
  PROP_0,
  PROP_ITEM,
  PROP_SELECTED,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

GtkWidget *
sp_process_model_row_new (SpProcessModelItem *item)
{
  return g_object_new (SP_TYPE_PROCESS_MODEL_ROW,
                       "item", item,
                       NULL);
}

SpProcessModelItem *
sp_process_model_row_get_item (SpProcessModelRow *self)
{
  SpProcessModelRowPrivate *priv = sp_process_model_row_get_instance_private (self);

  g_return_val_if_fail (SP_IS_PROCESS_MODEL_ROW (self), NULL);

  return priv->item;
}

static void
sp_process_model_row_set_item (SpProcessModelRow  *self,
                               SpProcessModelItem *item)
{
  SpProcessModelRowPrivate *priv = sp_process_model_row_get_instance_private (self);

  g_assert (SP_IS_PROCESS_MODEL_ROW (self));
  g_assert (SP_IS_PROCESS_MODEL_ITEM (item));

  if (g_set_object (&priv->item, item))
    {
      const gchar *command_line;
      g_auto(GStrv) parts = NULL;
      g_autofree gchar *pidstr = NULL;
      const gchar * const *argv;
      GPid pid;

      command_line = sp_process_model_item_get_command_line (item);
      parts = g_strsplit (command_line ?: "", "\n", 0);
      gtk_label_set_label (priv->label, parts [0]);

      if ((NULL != (argv = sp_process_model_item_get_argv (item))) && (argv[0] != NULL))
        {
          g_autofree gchar *argvstr = g_strjoinv (" ", (gchar **)&argv[1]);

          gtk_label_set_label (priv->args_label, argvstr);
        }

      pid = sp_process_model_item_get_pid (item);
      pidstr = g_strdup_printf ("<small>%u</small>", pid);
      gtk_label_set_label (priv->pid, pidstr);
      gtk_label_set_use_markup (priv->pid, TRUE);
    }
}

gboolean
sp_process_model_row_get_selected (SpProcessModelRow *self)
{
  SpProcessModelRowPrivate *priv = sp_process_model_row_get_instance_private (self);

  g_return_val_if_fail (SP_IS_PROCESS_MODEL_ROW (self), FALSE);

  return gtk_widget_get_visible (GTK_WIDGET (priv->check));
}

void
sp_process_model_row_set_selected (SpProcessModelRow *self,
                                   gboolean           selected)
{
  SpProcessModelRowPrivate *priv = sp_process_model_row_get_instance_private (self);

  g_return_if_fail (SP_IS_PROCESS_MODEL_ROW (self));

  selected = !!selected;

  if (selected != sp_process_model_row_get_selected (self))
    {
      gtk_widget_set_visible (GTK_WIDGET (priv->check), selected);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_SELECTED]);
    }
}

static gboolean
sp_process_model_row_query_tooltip (GtkWidget  *widget,
                                    gint        x,
                                    gint        y,
                                    gboolean    keyboard_mode,
                                    GtkTooltip *tooltip)
{
  SpProcessModelRow *self = (SpProcessModelRow *)widget;
  SpProcessModelRowPrivate *priv = sp_process_model_row_get_instance_private (self);

  g_assert (SP_IS_PROCESS_MODEL_ROW (self));
  g_assert (GTK_IS_TOOLTIP (tooltip));

  if (priv->item != NULL)
    {
      const gchar * const *argv = sp_process_model_item_get_argv (priv->item);

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
sp_process_model_row_finalize (GObject *object)
{
  SpProcessModelRow *self = (SpProcessModelRow *)object;
  SpProcessModelRowPrivate *priv = sp_process_model_row_get_instance_private (self);

  g_clear_object (&priv->item);

  G_OBJECT_CLASS (sp_process_model_row_parent_class)->finalize (object);
}

static void
sp_process_model_row_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  SpProcessModelRow *self = SP_PROCESS_MODEL_ROW (object);

  switch (prop_id)
    {
    case PROP_ITEM:
      g_value_set_object (value, sp_process_model_row_get_item (self));
      break;

    case PROP_SELECTED:
      g_value_set_boolean (value, sp_process_model_row_get_selected (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_process_model_row_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  SpProcessModelRow *self = SP_PROCESS_MODEL_ROW (object);

  switch (prop_id)
    {
    case PROP_ITEM:
      sp_process_model_row_set_item (self, g_value_get_object (value));
      break;

    case PROP_SELECTED:
      sp_process_model_row_set_selected (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_process_model_row_class_init (SpProcessModelRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = sp_process_model_row_finalize;
  object_class->get_property = sp_process_model_row_get_property;
  object_class->set_property = sp_process_model_row_set_property;

  widget_class->query_tooltip = sp_process_model_row_query_tooltip;

  properties [PROP_ITEM] =
    g_param_spec_object ("item",
                         "Item",
                         "Item",
                         SP_TYPE_PROCESS_MODEL_ITEM,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SELECTED] =
    g_param_spec_boolean ("selected",
                          "Selected",
                          "Selected",
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/sysprof/ui/sp-process-model-row.ui");
  gtk_widget_class_bind_template_child_private (widget_class, SpProcessModelRow, args_label);
  gtk_widget_class_bind_template_child_private (widget_class, SpProcessModelRow, image);
  gtk_widget_class_bind_template_child_private (widget_class, SpProcessModelRow, label);
  gtk_widget_class_bind_template_child_private (widget_class, SpProcessModelRow, pid);
  gtk_widget_class_bind_template_child_private (widget_class, SpProcessModelRow, check);
}

static void
sp_process_model_row_init (SpProcessModelRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_widget_set_has_tooltip (GTK_WIDGET (self), TRUE);
}
