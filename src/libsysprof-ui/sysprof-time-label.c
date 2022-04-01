/* sysprof-time-label.c
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

#define G_LOG_DOMAIN "sysprof-time-label"

#include "config.h"

#include "sysprof-time-label.h"

struct _SysprofTimeLabel
{
  GtkWidget     parent_instance;
  GtkCenterBox *box;
  GtkLabel     *minutes;
  GtkLabel     *seconds;
};

G_DEFINE_TYPE (SysprofTimeLabel, sysprof_time_label, GTK_TYPE_WIDGET)

static void
sysprof_time_label_dispose (GObject *object)
{
  SysprofTimeLabel *self = (SysprofTimeLabel *)object;

  if (self->box)
    {
      gtk_widget_unparent (GTK_WIDGET (self->box));
      self->box = NULL;
    }

  G_OBJECT_CLASS (sysprof_time_label_parent_class)->dispose (object);
}

static void
sysprof_time_label_class_init (SysprofTimeLabelClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = sysprof_time_label_dispose;

  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
}

static void
sysprof_time_label_init (SysprofTimeLabel *self)
{
  PangoAttrList *attrs = pango_attr_list_new ();
  GtkWidget *sep;

  pango_attr_list_insert (attrs, pango_attr_scale_new (4));
  pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));

  self->box = GTK_CENTER_BOX (gtk_center_box_new ());
  gtk_widget_set_parent (GTK_WIDGET (self->box), GTK_WIDGET (self));

  self->minutes = g_object_new (GTK_TYPE_LABEL,
                                "attributes", attrs,
                                "xalign", 1.0f,
                                "hexpand", TRUE,
                                NULL);
  gtk_center_box_set_start_widget (self->box, GTK_WIDGET (self->minutes));

  sep = g_object_new (GTK_TYPE_LABEL,
                      "margin-start", 3,
                      "margin-end", 3,
                      "attributes", attrs,
                      "visible", TRUE,
                      "label", ":",
                      NULL);
  gtk_center_box_set_center_widget (self->box, sep);

  self->seconds = g_object_new (GTK_TYPE_LABEL,
                                "attributes", attrs,
                                "visible", TRUE,
                                "xalign", 0.0f,
                                "hexpand", TRUE,
                                NULL);
  gtk_center_box_set_end_widget (self->box, GTK_WIDGET (self->seconds));
}

void
sysprof_time_label_set_duration (SysprofTimeLabel *self,
                                 guint             duration)
{
  gchar minstr[12];
  gchar secstr[12];
  gint min, sec;

  g_return_if_fail (SYSPROF_IS_TIME_LABEL (self));

  min = duration / 60;
  sec = duration % 60;

  g_snprintf (minstr, sizeof minstr, "%02d", min);
  g_snprintf (secstr, sizeof secstr, "%02d", sec);

  gtk_label_set_label (self->minutes, minstr);
  gtk_label_set_label (self->seconds, secstr);
}
