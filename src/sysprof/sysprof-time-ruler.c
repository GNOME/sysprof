/* sysprof-time-ruler.c
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

#include "sysprof-time-ruler.h"

#define GROUP_SIZE 100

struct _SysprofTimeRuler
{
  GtkWidget       parent_instance;
  SysprofSession *session;
  GSignalGroup   *session_signals;
};

enum {
  PROP_0,
  PROP_SESSION,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofTimeRuler, sysprof_time_ruler, GTK_TYPE_WIDGET)

static GParamSpec *properties [N_PROPS];

static void
sysprof_time_ruler_snapshot (GtkWidget   *widget,
                             GtkSnapshot *snapshot)
{
  SysprofTimeRuler *self = (SysprofTimeRuler *)widget;
  const SysprofTimeSpan *visible_time;
  const SysprofTimeSpan *document_time;
  PangoLayout *layout;
  gint64 tick_interval;
  gint64 duration;
  gint64 rem;
  int last_x = G_MININT;
  int width;
  int height;
  GdkRGBA color;
  GdkRGBA line_color;

  g_assert (SYSPROF_IS_TIME_RULER (self));
  g_assert (GTK_IS_SNAPSHOT (snapshot));
  g_assert (!self->session || SYSPROF_IS_SESSION (self->session));

  width = gtk_widget_get_width (widget);
  height = gtk_widget_get_height (widget);

  if (self->session == NULL || width == 0 || height == 0)
    return;

  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  {
    GtkStyleContext *style_context = gtk_widget_get_style_context (widget);
    gtk_style_context_get_color (style_context, &color);
    line_color = color;
    line_color.alpha *= .3;
  }
  G_GNUC_BEGIN_IGNORE_DEPRECATIONS

  document_time = sysprof_session_get_document_time (self->session);
  visible_time = sysprof_session_get_visible_time (self->session);
  duration = sysprof_time_span_duration (*visible_time);

  layout = gtk_widget_create_pango_layout (widget, NULL);

  if (duration > SYSPROF_NSEC_PER_SEC*60)
    tick_interval = SYSPROF_NSEC_PER_SEC*10;
  else if (duration > SYSPROF_NSEC_PER_SEC*30)
    tick_interval = SYSPROF_NSEC_PER_SEC*5;
  else if (duration > SYSPROF_NSEC_PER_SEC*15)
    tick_interval = SYSPROF_NSEC_PER_SEC*2.5;
  else if (duration > SYSPROF_NSEC_PER_SEC*5)
    tick_interval = SYSPROF_NSEC_PER_SEC;
  else if (duration > SYSPROF_NSEC_PER_SEC*2.5)
    tick_interval = SYSPROF_NSEC_PER_SEC*.5;
  else if (duration > SYSPROF_NSEC_PER_SEC)
    tick_interval = SYSPROF_NSEC_PER_SEC*.25;
  else if (duration > SYSPROF_NSEC_PER_SEC*.5)
    tick_interval = SYSPROF_NSEC_PER_SEC*.1;
  else if (duration > SYSPROF_NSEC_PER_SEC*.25)
    tick_interval = SYSPROF_NSEC_PER_SEC*.05;
  else if (duration > SYSPROF_NSEC_PER_SEC*.1)
    tick_interval = SYSPROF_NSEC_PER_SEC*.02;
  else if (duration > SYSPROF_NSEC_PER_SEC*.05)
    tick_interval = SYSPROF_NSEC_PER_SEC*.01;
  else if (duration > SYSPROF_NSEC_PER_SEC*.01)
    tick_interval = SYSPROF_NSEC_PER_SEC*.005;
  else if (duration > SYSPROF_NSEC_PER_SEC*.005)
    tick_interval = SYSPROF_NSEC_PER_SEC*.001;
  else if (duration > SYSPROF_NSEC_PER_SEC*.001)
    tick_interval = SYSPROF_NSEC_PER_SEC*.0005;
  else if (duration > SYSPROF_NSEC_PER_SEC*.0005)
    tick_interval = SYSPROF_NSEC_PER_SEC*.00001;
  else
    tick_interval = SYSPROF_NSEC_PER_SEC / 1000000;

  rem = (visible_time->begin_nsec - document_time->begin_nsec) % tick_interval;

  for (gint64 t = visible_time->begin_nsec - rem + tick_interval;
       t < visible_time->end_nsec;
       t += tick_interval)
    {
      gint64 o = t - visible_time->begin_nsec;
      gint64 r = t - document_time->begin_nsec;
      double x = (o / (double)duration) * width;
      int pw, ph;
      char str[32];

      if (x >= 0 && (x - 6) >= last_x)
        {
          if (r == 0)
            g_snprintf (str, sizeof str, "%.3lf s", .0);
          else if (r < 1000000)
            g_snprintf (str, sizeof str, "%.3lf μs", r/1000.);
          else if (r < SYSPROF_NSEC_PER_SEC)
            g_snprintf (str, sizeof str, "%.3lf ms", r/1000000.);
          else if (duration < SYSPROF_NSEC_PER_SEC/1000)
            g_snprintf (str, sizeof str, "%.6lf s", r/(double)SYSPROF_NSEC_PER_SEC);
          else if (duration < SYSPROF_NSEC_PER_SEC/100)
            g_snprintf (str, sizeof str, "%.5lf s", r/(double)SYSPROF_NSEC_PER_SEC);
          else if (duration < SYSPROF_NSEC_PER_SEC/10)
            g_snprintf (str, sizeof str, "%.4lf s", r/(double)SYSPROF_NSEC_PER_SEC);
          else
            g_snprintf (str, sizeof str, "%.3lf s", r/(double)SYSPROF_NSEC_PER_SEC);

          pango_layout_set_text (layout, str, -1);
          pango_layout_get_pixel_size (layout, &pw, &ph);

          gtk_snapshot_save (snapshot);
          gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (x-pw-6, (height-ph)/2));
          gtk_snapshot_append_layout (snapshot, layout, &color);
          gtk_snapshot_restore (snapshot);

          gtk_snapshot_append_color (snapshot,
                                     &line_color,
                                     &GRAPHENE_RECT_INIT (x, 0, 1, height));

          last_x = x + pw + 6;
        }
    }

  g_object_unref (layout);
}

static void
sysprof_time_ruler_measure (GtkWidget      *widget,
                            GtkOrientation  orientation,
                            int             for_size,
                            int            *minimum,
                            int            *natural,
                            int            *minimum_baseline,
                            int            *natural_baseline)
{
  GTK_WIDGET_CLASS (sysprof_time_ruler_parent_class)->measure (widget,
                                                               orientation,
                                                               for_size,
                                                               minimum,
                                                               natural,
                                                               minimum_baseline,
                                                               natural_baseline);

  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    {
      *minimum = MAX (*minimum, GROUP_SIZE);
      *natural = MAX (*natural, *minimum);
    }
}

static void
sysprof_time_ruler_dispose (GObject *object)
{
  SysprofTimeRuler *self = (SysprofTimeRuler *)object;

  g_signal_group_set_target (self->session_signals, NULL);
  g_clear_object (&self->session);

  G_OBJECT_CLASS (sysprof_time_ruler_parent_class)->dispose (object);
}

static void
sysprof_time_ruler_finalize (GObject *object)
{
  SysprofTimeRuler *self = (SysprofTimeRuler *)object;

  g_clear_object (&self->session_signals);

  G_OBJECT_CLASS (sysprof_time_ruler_parent_class)->finalize (object);
}

static void
sysprof_time_ruler_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  SysprofTimeRuler *self = SYSPROF_TIME_RULER (object);

  switch (prop_id)
    {
    case PROP_SESSION:
      g_value_set_object (value, sysprof_time_ruler_get_session (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_time_ruler_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  SysprofTimeRuler *self = SYSPROF_TIME_RULER (object);

  switch (prop_id)
    {
    case PROP_SESSION:
      sysprof_time_ruler_set_session (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_time_ruler_class_init (SysprofTimeRulerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_time_ruler_dispose;
  object_class->finalize = sysprof_time_ruler_finalize;
  object_class->get_property = sysprof_time_ruler_get_property;
  object_class->set_property = sysprof_time_ruler_set_property;

  widget_class->snapshot = sysprof_time_ruler_snapshot;
  widget_class->measure = sysprof_time_ruler_measure;

  properties [PROP_SESSION] =
    g_param_spec_object ("session", NULL, NULL,
                         SYSPROF_TYPE_SESSION,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_css_name (widget_class, "timeruler");
}

static void
sysprof_time_ruler_init (SysprofTimeRuler *self)
{
  self->session_signals = g_signal_group_new (SYSPROF_TYPE_SESSION);
  g_signal_group_connect_object (self->session_signals,
                                 "notify::selected-time",
                                 G_CALLBACK (gtk_widget_queue_draw),
                                 self,
                                 G_CONNECT_SWAPPED);
  g_signal_group_connect_object (self->session_signals,
                                 "notify::visible-time",
                                 G_CALLBACK (gtk_widget_queue_draw),
                                 self,
                                 G_CONNECT_SWAPPED);
  g_signal_connect_object (self->session_signals,
                           "bind",
                           G_CALLBACK (gtk_widget_queue_draw),
                           self,
                           G_CONNECT_SWAPPED);

  gtk_widget_set_cursor_from_name (GTK_WIDGET (self), "text");
}

GtkWidget *
sysprof_time_ruler_new (void)
{
  return g_object_new (SYSPROF_TYPE_TIME_RULER, NULL);
}

/**
 * sysprof_time_ruler_get_session:
 * @self: a #SysprofTimeRuler
 *
 * Returns: (transfer none) (nullable): a #SysprofSession or %NULL
 */
SysprofSession *
sysprof_time_ruler_get_session (SysprofTimeRuler *self)
{
  g_return_val_if_fail (SYSPROF_IS_TIME_RULER (self), NULL);

  return self->session;
}

void
sysprof_time_ruler_set_session (SysprofTimeRuler *self,
                                SysprofSession   *session)
{
  g_return_if_fail (SYSPROF_IS_TIME_RULER (self));

  if (g_set_object (&self->session, session))
    {
      g_signal_group_set_target (self->session_signals, session);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SESSION]);
    }
}

char *
sysprof_time_ruler_get_label_at_point (SysprofTimeRuler *self,
                                       double            x)
{
  char str[32];
  const SysprofTimeSpan *document_time;
  const SysprofTimeSpan *visible;
  gint64 duration;
  gint64 o;
  int width;

  g_return_val_if_fail (SYSPROF_IS_TIME_RULER (self), NULL);

  if (x < 0)
    return NULL;

  if (x > gtk_widget_get_width (GTK_WIDGET (self)))
    return NULL;

  if (self->session == NULL)
    return NULL;

  if (!(document_time = sysprof_session_get_document_time (self->session)) ||
      !(visible = sysprof_session_get_visible_time (self->session)) ||
      !(duration = sysprof_time_span_duration (*visible)))
    return NULL;

  width = gtk_widget_get_width (GTK_WIDGET (self));
  o = visible->begin_nsec + (x / (double)width * duration) - document_time->begin_nsec;

  if (o == 0)
    g_snprintf (str, sizeof str, "%.3lf s", .0);
  else if (o < 1000000)
    g_snprintf (str, sizeof str, "%.3lf μs", o/1000.);
  else if (o < SYSPROF_NSEC_PER_SEC)
    g_snprintf (str, sizeof str, "%.3lf ms", o/1000000.);
  else if (duration < SYSPROF_NSEC_PER_SEC/1000)
    g_snprintf (str, sizeof str, "%.6lf s", o/(double)SYSPROF_NSEC_PER_SEC);
  else if (duration < SYSPROF_NSEC_PER_SEC/100)
    g_snprintf (str, sizeof str, "%.5lf s", o/(double)SYSPROF_NSEC_PER_SEC);
  else if (duration < SYSPROF_NSEC_PER_SEC/10)
    g_snprintf (str, sizeof str, "%.4lf s", o/(double)SYSPROF_NSEC_PER_SEC);
  else
    g_snprintf (str, sizeof str, "%.3lf s", o/(double)SYSPROF_NSEC_PER_SEC);

  return g_strdup (str);
}
