/* sysprof-recording-state-view.c
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

#include "sysprof-recording-state-view.h"
#include "sysprof-time-label.h"

typedef struct
{
  SysprofProfiler  *profiler;
  SysprofTimeLabel *elapsed;
  GtkLabel         *samples;
  gulong            notify_elapsed_handler;
} SysprofRecordingStateViewPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (SysprofRecordingStateView, sysprof_recording_state_view, GTK_TYPE_BIN)

enum {
  PROP_0,
  PROP_PROFILER,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

GtkWidget *
sysprof_recording_state_view_new (void)
{
  return g_object_new (SYSPROF_TYPE_RECORDING_STATE_VIEW, NULL);
}

static void
sysprof_recording_state_view_notify_elapsed (SysprofRecordingStateView *self,
                                             GParamSpec                *pspec,
                                             SysprofProfiler           *profiler)
{
  SysprofRecordingStateViewPrivate *priv = sysprof_recording_state_view_get_instance_private (self);
  SysprofCaptureWriter *writer;
  gint64 elapsed;

  g_assert (SYSPROF_IS_RECORDING_STATE_VIEW (self));
  g_assert (SYSPROF_IS_PROFILER (profiler));

  if ((writer = sysprof_profiler_get_writer (profiler)))
    {
      SysprofCaptureStat st;
      g_autofree gchar *samples = NULL;
      gint64 count;

      sysprof_capture_writer_stat (writer, &st);
      count = st.frame_count[SYSPROF_CAPTURE_FRAME_SAMPLE] +
              st.frame_count[SYSPROF_CAPTURE_FRAME_MARK] +
              st.frame_count[SYSPROF_CAPTURE_FRAME_CTRSET];

      samples = g_strdup_printf ("%"G_GINT64_FORMAT, count);
      gtk_label_set_label (priv->samples, samples);
    }

  elapsed = (gint64)sysprof_profiler_get_elapsed (profiler);
  sysprof_time_label_set_duration (priv->elapsed, elapsed);
}

static void
sysprof_recording_state_view_destroy (GtkWidget *widget)
{
  SysprofRecordingStateView *self = (SysprofRecordingStateView *)widget;
  SysprofRecordingStateViewPrivate *priv = sysprof_recording_state_view_get_instance_private (self);

  if (priv->profiler != NULL)
    {
      g_signal_handler_disconnect (priv->profiler, priv->notify_elapsed_handler);
      g_clear_object (&priv->profiler);
    }

  GTK_WIDGET_CLASS (sysprof_recording_state_view_parent_class)->destroy (widget);
}

static void
sysprof_recording_state_view_get_property (GObject    *object,
                                           guint       prop_id,
                                           GValue     *value,
                                           GParamSpec *pspec)
{
  SysprofRecordingStateView *self = SYSPROF_RECORDING_STATE_VIEW (object);
  SysprofRecordingStateViewPrivate *priv = sysprof_recording_state_view_get_instance_private (self);

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
sysprof_recording_state_view_set_property (GObject      *object,
                                           guint         prop_id,
                                           const GValue *value,
                                           GParamSpec   *pspec)
{
  SysprofRecordingStateView *self = SYSPROF_RECORDING_STATE_VIEW (object);

  switch (prop_id)
    {
    case PROP_PROFILER:
      sysprof_recording_state_view_set_profiler (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_recording_state_view_class_init (SysprofRecordingStateViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = sysprof_recording_state_view_get_property;
  object_class->set_property = sysprof_recording_state_view_set_property;

  widget_class->destroy = sysprof_recording_state_view_destroy;

  properties [PROP_PROFILER] =
    g_param_spec_object ("profiler",
                         "Profiler",
                         "Profiler",
                         SYSPROF_TYPE_PROFILER,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/ui/sysprof-recording-state-view.ui");
  gtk_widget_class_bind_template_child_private (widget_class, SysprofRecordingStateView, elapsed);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofRecordingStateView, samples);

  g_type_ensure (SYSPROF_TYPE_TIME_LABEL);
}

static void
sysprof_recording_state_view_init (SysprofRecordingStateView *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

void
sysprof_recording_state_view_set_profiler (SysprofRecordingStateView *self,
                                           SysprofProfiler           *profiler)
{
  SysprofRecordingStateViewPrivate *priv = sysprof_recording_state_view_get_instance_private (self);

  g_assert (SYSPROF_IS_RECORDING_STATE_VIEW (self));
  g_assert (!profiler || SYSPROF_IS_PROFILER (profiler));

  sysprof_time_label_set_duration (priv->elapsed, 0);

  if (profiler != priv->profiler)
    {
      if (priv->profiler != NULL)
        {
          g_signal_handler_disconnect (priv->profiler, priv->notify_elapsed_handler);
          g_clear_object (&priv->profiler);
        }

      if (profiler != NULL)
        {
          priv->profiler = g_object_ref (profiler);
          priv->notify_elapsed_handler =
            g_signal_connect_object (profiler,
                                     "notify::elapsed",
                                     G_CALLBACK (sysprof_recording_state_view_notify_elapsed),
                                     self,
                                     G_CONNECT_SWAPPED);
        }
    }
}
