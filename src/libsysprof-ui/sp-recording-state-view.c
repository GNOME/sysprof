/* sp-recording-state-view.c
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
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

#include "sp-recording-state-view.h"

typedef struct
{
  SpProfiler *profiler;
  gulong      notify_elapsed_handler;
  GtkLabel   *elapsed;
} SpRecordingStateViewPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (SpRecordingStateView, sp_recording_state_view, GTK_TYPE_BIN)

enum {
  PROP_0,
  PROP_PROFILER,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

GtkWidget *
sp_recording_state_view_new (void)
{
  return g_object_new (SP_TYPE_RECORDING_STATE_VIEW, NULL);
}

static void
sp_recording_state_view_notify_elapsed (SpRecordingStateView *self,
                                        GParamSpec           *pspec,
                                        SpProfiler           *profiler)
{
  SpRecordingStateViewPrivate *priv = sp_recording_state_view_get_instance_private (self);
  g_autofree gchar *str = NULL;
  gint64 elapsed;
  guint hours;
  guint minutes;
  guint seconds;

  g_assert (SP_IS_RECORDING_STATE_VIEW (self));
  g_assert (SP_IS_PROFILER (profiler));

  elapsed = (gint64)sp_profiler_get_elapsed (profiler);

  hours = elapsed / (60 * 60);
  if (hours > 0)
    minutes = (elapsed % (hours * 60 * 60)) / 60;
  else
    minutes = elapsed / 60;
  seconds = elapsed % 60;

  if (hours == 0)
    str = g_strdup_printf ("%02u:%02u", minutes, seconds);
  else
    str = g_strdup_printf ("%02u:%02u:%02u", hours, minutes, seconds);

  gtk_label_set_label (priv->elapsed, str);
}

static void
sp_recording_state_view_destroy (GtkWidget *widget)
{
  SpRecordingStateView *self = (SpRecordingStateView *)widget;
  SpRecordingStateViewPrivate *priv = sp_recording_state_view_get_instance_private (self);

  if (priv->profiler != NULL)
    {
      g_signal_handler_disconnect (priv->profiler, priv->notify_elapsed_handler);
      g_clear_object (&priv->profiler);
    }

  GTK_WIDGET_CLASS (sp_recording_state_view_parent_class)->destroy (widget);
}

static void
sp_recording_state_view_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  SpRecordingStateView *self = SP_RECORDING_STATE_VIEW (object);
  SpRecordingStateViewPrivate *priv = sp_recording_state_view_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_PROFILER:
      g_value_set_object (value, priv->profiler);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_recording_state_view_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  SpRecordingStateView *self = SP_RECORDING_STATE_VIEW (object);

  switch (prop_id)
    {
    case PROP_PROFILER:
      sp_recording_state_view_set_profiler (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_recording_state_view_class_init (SpRecordingStateViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = sp_recording_state_view_get_property;
  object_class->set_property = sp_recording_state_view_set_property;

  widget_class->destroy = sp_recording_state_view_destroy;

  properties [PROP_PROFILER] =
    g_param_spec_object ("profiler",
                         "Profiler",
                         "Profiler",
                         SP_TYPE_PROFILER,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/sysprof/ui/sp-recording-state-view.ui");
  gtk_widget_class_bind_template_child_private (widget_class, SpRecordingStateView, elapsed);
}

static void
sp_recording_state_view_init (SpRecordingStateView *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

void
sp_recording_state_view_set_profiler (SpRecordingStateView *self,
                                      SpProfiler           *profiler)
{
  SpRecordingStateViewPrivate *priv = sp_recording_state_view_get_instance_private (self);

  g_assert (SP_IS_RECORDING_STATE_VIEW (self));
  g_assert (!profiler || SP_IS_PROFILER (profiler));

  gtk_label_set_label (priv->elapsed, "00:00");

  if (profiler != priv->profiler)
    {
      if (priv->profiler != NULL)
        {
          g_signal_handler_disconnect (priv->profiler, priv->notify_elapsed_handler);
          g_clear_object (&priv->profiler);
        }

      gtk_label_set_label (priv->elapsed, "00:00");

      if (profiler != NULL)
        {
          priv->profiler = g_object_ref (profiler);
          priv->notify_elapsed_handler =
            g_signal_connect_object (profiler,
                                     "notify::elapsed",
                                     G_CALLBACK (sp_recording_state_view_notify_elapsed),
                                     self,
                                     G_CONNECT_SWAPPED);
        }
    }
}
