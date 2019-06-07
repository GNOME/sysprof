/* sysprof-aid-icon.c
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

#define G_LOG_DOMAIN "sysprof-aid-icon"

#include "config.h"

#include "sysprof-aid-icon.h"

struct _SysprofAidIcon
{
  GtkFlowBoxChild parent_instance;

  SysprofAid *aid;

  /* Template Objects */
  GtkLabel *label;
  GtkImage *image;
  GtkImage *check;
};

G_DEFINE_TYPE (SysprofAidIcon, sysprof_aid_icon, GTK_TYPE_FLOW_BOX_CHILD)

enum {
  PROP_0,
  PROP_AID,
  PROP_SELECTED,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

/**
 * sysprof_aid_icon_new:
 *
 * Create a new #SysprofAidIcon.
 *
 * Returns: (transfer full): a newly created #SysprofAidIcon
 */
GtkWidget *
sysprof_aid_icon_new (SysprofAid *aid)
{
  g_return_val_if_fail (SYSPROF_IS_AID (aid), NULL);

  return g_object_new (SYSPROF_TYPE_AID_ICON,
                       "aid", aid,
                       NULL);
}

gboolean
sysprof_aid_icon_is_selected (SysprofAidIcon *self)
{
  g_return_val_if_fail (SYSPROF_IS_AID_ICON (self), FALSE);

  return gtk_widget_get_visible (GTK_WIDGET (self->check));
}

/**
 * sysprof_aid_icon_get_aid:
 *
 * Get the aid that is represented by the icon.
 *
 * Returns: (transfer none): a #SysprofAid
 *
 * Since: 3.34
 */
SysprofAid *
sysprof_aid_icon_get_aid (SysprofAidIcon *self)
{
  g_return_val_if_fail (SYSPROF_IS_AID_ICON (self), NULL);

  return self->aid;
}

static void
sysprof_aid_icon_set_aid (SysprofAidIcon *self,
                          SysprofAid     *aid)
{
  g_return_if_fail (SYSPROF_IS_AID_ICON (self));
  g_return_if_fail (SYSPROF_IS_AID (aid));

  if (g_set_object (&self->aid, aid))
    {
      GIcon *icon = sysprof_aid_get_icon (aid);
      const gchar *title = sysprof_aid_get_display_name (aid);

      g_object_set (self->image, "gicon", icon, NULL);
      gtk_label_set_label (self->label, title);
    }
}

static void
sysprof_aid_icon_finalize (GObject *object)
{
  SysprofAidIcon *self = (SysprofAidIcon *)object;

  g_clear_object (&self->aid);

  G_OBJECT_CLASS (sysprof_aid_icon_parent_class)->finalize (object);
}

static void
sysprof_aid_icon_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  SysprofAidIcon *self = SYSPROF_AID_ICON (object);

  switch (prop_id)
    {
    case PROP_AID:
      g_value_set_object (value, sysprof_aid_icon_get_aid (self));
      break;

    case PROP_SELECTED:
      g_value_set_boolean (value, gtk_widget_get_visible (GTK_WIDGET (self->check)));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_aid_icon_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  SysprofAidIcon *self = SYSPROF_AID_ICON (object);

  switch (prop_id)
    {
    case PROP_AID:
      sysprof_aid_icon_set_aid (self, g_value_get_object (value));
      break;

    case PROP_SELECTED:
      gtk_widget_set_visible (GTK_WIDGET (self->check), g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_aid_icon_class_init (SysprofAidIconClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = sysprof_aid_icon_finalize;
  object_class->get_property = sysprof_aid_icon_get_property;
  object_class->set_property = sysprof_aid_icon_set_property;

  properties [PROP_AID] =
    g_param_spec_object ("aid",
                         "Aid",
                         "The aid for the icon",
                         SYSPROF_TYPE_AID,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_SELECTED] =
    g_param_spec_boolean ("selected",
                          "Selected",
                          "If the item is selected",
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_css_name (widget_class, "sysprofaidicon");
  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/ui/sysprof-aid-icon.ui");
  gtk_widget_class_bind_template_child (widget_class, SysprofAidIcon, check);
  gtk_widget_class_bind_template_child (widget_class, SysprofAidIcon, image);
  gtk_widget_class_bind_template_child (widget_class, SysprofAidIcon, label);
}

static void
sysprof_aid_icon_init (SysprofAidIcon *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

void
sysprof_aid_icon_toggle (SysprofAidIcon *self)
{
  g_return_if_fail (SYSPROF_IS_AID_ICON (self));

  gtk_widget_set_visible (GTK_WIDGET (self->check),
                          !gtk_widget_get_visible (GTK_WIDGET (self->check)));
}
