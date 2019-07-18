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
  GtkBox parent;

  GtkLabel *minutes;
  GtkLabel *seconds;
};

G_DEFINE_TYPE (SysprofTimeLabel, sysprof_time_label, GTK_TYPE_BOX)

static void
sysprof_time_label_class_init (SysprofTimeLabelClass *klass)
{
}

static void
sysprof_time_label_init (SysprofTimeLabel *self)
{
  PangoAttrList *attrs = pango_attr_list_new ();
  GtkWidget *sep;

  pango_attr_list_insert (attrs, pango_attr_scale_new (4));
  pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));

  gtk_box_set_spacing (GTK_BOX (self), 3);

  self->minutes = g_object_new (GTK_TYPE_LABEL,
                                "attributes", attrs,
                                "visible", TRUE,
                                "xalign", 1.0f,
                                NULL);
  gtk_container_add_with_properties (GTK_CONTAINER (self), GTK_WIDGET (self->minutes),
                                     "pack-type", GTK_PACK_START,
                                     "expand", TRUE,
                                     "fill", TRUE,
                                     NULL);

  sep = g_object_new (GTK_TYPE_LABEL,
                      "attributes", attrs,
                      "visible", TRUE,
                      "label", ":",
                      NULL);
  gtk_box_set_center_widget (GTK_BOX (self), sep);

  self->seconds = g_object_new (GTK_TYPE_LABEL,
                                "attributes", attrs,
                                "visible", TRUE,
                                "xalign", 0.0f,
                                NULL);
  gtk_container_add_with_properties (GTK_CONTAINER (self), GTK_WIDGET (self->seconds),
                                     "pack-type", GTK_PACK_END,
                                     "expand", TRUE,
                                     "fill", TRUE,
                                     NULL);
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
