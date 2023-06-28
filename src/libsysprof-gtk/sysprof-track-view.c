/* sysprof-track-view.c
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

#include "sysprof-track-view.h"

struct _SysprofTrackView
{
  GtkWidget     parent_instance;
  SysprofTrack *track;
};

enum {
  PROP_0,
  PROP_TRACK,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofTrackView, sysprof_track_view, GTK_TYPE_WIDGET)

static GParamSpec *properties [N_PROPS];

static void
sysprof_track_view_dispose (GObject *object)
{
  SysprofTrackView *self = (SysprofTrackView *)object;
  GtkWidget *child;

  gtk_widget_dispose_template (GTK_WIDGET (self), SYSPROF_TYPE_TRACK_VIEW);

  g_clear_object (&self->track);

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self))))
    gtk_widget_unparent (child);

  G_OBJECT_CLASS (sysprof_track_view_parent_class)->dispose (object);
}

static void
sysprof_track_view_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  SysprofTrackView *self = SYSPROF_TRACK_VIEW (object);

  switch (prop_id)
    {
    case PROP_TRACK:
      g_value_set_object (value, sysprof_track_view_get_track (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_track_view_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  SysprofTrackView *self = SYSPROF_TRACK_VIEW (object);

  switch (prop_id)
    {
    case PROP_TRACK:
      sysprof_track_view_set_track (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_track_view_class_init (SysprofTrackViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_track_view_dispose;
  object_class->get_property = sysprof_track_view_get_property;
  object_class->set_property = sysprof_track_view_set_property;

  properties[PROP_TRACK] =
    g_param_spec_object ("track", NULL, NULL,
                         SYSPROF_TYPE_TRACK,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/libsysprof-gtk/sysprof-track-view.ui");
  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);

  g_type_ensure (SYSPROF_TYPE_TRACK);
}

static void
sysprof_track_view_init (SysprofTrackView *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
sysprof_track_view_new (void)
{
  return g_object_new (SYSPROF_TYPE_TRACK_VIEW, NULL);
}

/**
 * sysprof_track_view_get_track:
 * @self: a #SysprofTrackView
 *
 * Returns: (transfer none) (nullable): A #SysprofTrack or %NULL
 */
SysprofTrack *
sysprof_track_view_get_track (SysprofTrackView *self)
{
  g_return_val_if_fail (SYSPROF_IS_TRACK_VIEW (self), NULL);

  return self->track;
}

void
sysprof_track_view_set_track (SysprofTrackView *self,
                              SysprofTrack     *track)
{
  g_return_if_fail (SYSPROF_IS_TRACK_VIEW (self));
  g_return_if_fail (!track || SYSPROF_IS_TRACK (track));

  if (g_set_object (&self->track, track))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TRACK]);
}
