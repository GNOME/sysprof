/* sysprof-frame-utility.c
 *
 * Copyright 2023 Christian Hergert <chergert@redhat.com>
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

#include "sysprof-frame-utility.h"
#include "sysprof-session.h"

struct _SysprofFrameUtility
{
  GtkWidget             parent_instance;
  SysprofSession       *session;
  SysprofDocumentFrame *frame;
};

enum {
  PROP_0,
  PROP_SESSION,
  PROP_FRAME,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofFrameUtility, sysprof_frame_utility, GTK_TYPE_WIDGET)

static GParamSpec *properties[N_PROPS];

static char *
format_time_offset (SysprofFrameUtility *self,
                    gint64               time_offset)
{
  int hours;
  int minutes;
  int seconds;
  double time;

  g_assert (SYSPROF_IS_FRAME_UTILITY (self));

  time = time_offset / (double)SYSPROF_NSEC_PER_SEC;

  hours = time / (60 * 60);
  time -= hours * (60 * 60);

  minutes = time / 60;
  time -= minutes * 60;

  seconds = time / SYSPROF_NSEC_PER_SEC;
  time -= seconds * SYSPROF_NSEC_PER_SEC;

  return g_strdup_printf ("%02d:%02d:%02d.%04d", hours, minutes, seconds, (int)(time * 10000));
}

static void
sysprof_frame_utility_finalize (GObject *object)
{
  SysprofFrameUtility *self = (SysprofFrameUtility *)object;
  GtkWidget *child;

  gtk_widget_dispose_template (GTK_WIDGET (self), SYSPROF_TYPE_FRAME_UTILITY);

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (object))))
    gtk_widget_unparent (child);

  g_clear_object (&self->session);
  g_clear_object (&self->frame);

  G_OBJECT_CLASS (sysprof_frame_utility_parent_class)->finalize (object);
}

static void
sysprof_frame_utility_get_property (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
  SysprofFrameUtility *self = SYSPROF_FRAME_UTILITY (object);

  switch (prop_id)
    {
    case PROP_SESSION:
      g_value_set_object (value, self->session);
      break;

    case PROP_FRAME:
      g_value_set_object (value, self->frame);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_frame_utility_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
  SysprofFrameUtility *self = SYSPROF_FRAME_UTILITY (object);

  switch (prop_id)
    {
    case PROP_SESSION:
      g_set_object (&self->session, g_value_get_object (value));
      break;

    case PROP_FRAME:
      g_set_object (&self->frame, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_frame_utility_class_init (SysprofFrameUtilityClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = sysprof_frame_utility_finalize;
  object_class->get_property = sysprof_frame_utility_get_property;
  object_class->set_property = sysprof_frame_utility_set_property;

  properties[PROP_SESSION] =
    g_param_spec_object ("session", NULL, NULL,
                         SYSPROF_TYPE_SESSION,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_FRAME] =
    g_param_spec_object ("frame", NULL, NULL,
                         SYSPROF_TYPE_DOCUMENT_FRAME,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-frame-utility.ui");
  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_bind_template_callback (widget_class, format_time_offset);
}

static void
sysprof_frame_utility_init (SysprofFrameUtility *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}
