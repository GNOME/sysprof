/* sysprof-visualizer.c
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

#define G_LOG_DOMAIN "sysprof-visualizer"

#include "config.h"

#include "sysprof-visualizer.h"

typedef struct
{
  gchar *title;

  gint64 begin_time;
  gint64 end_time;
  gint64 duration;

  /* A cached allocation that has the borders subtracted so that
   * we place the content within the expected area.
   */
  GtkAllocation cache_alloc;
} SysprofVisualizerPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (SysprofVisualizer, sysprof_visualizer, DZL_TYPE_BIN)

enum {
  PROP_0,
  PROP_BEGIN_TIME,
  PROP_END_TIME,
  PROP_TITLE,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

static inline void
subtract_border (GtkAllocation *alloc,
                 GtkBorder     *border)
{
#if 0
  g_print ("Border; %d %d %d %d\n", border->top, border->left, border->bottom, border->right);
#endif

  alloc->x += border->left;
  alloc->y += border->top;
  alloc->width -= border->left + border->right;
  alloc->height -= border->top + border->bottom;
}

static void
adjust_alloc_for_borders (SysprofVisualizer *self,
                          GtkAllocation     *alloc)
{
  GtkStyleContext *style_context;
  GtkBorder border;
  GtkStateFlags state;

  g_assert (SYSPROF_IS_VISUALIZER (self));
  g_assert (alloc != NULL);

  state = gtk_widget_get_state_flags (GTK_WIDGET (self));
  style_context = gtk_widget_get_style_context (GTK_WIDGET (self));
  gtk_style_context_get_border (style_context, state, &border);

  subtract_border (alloc, &border);
}

static void
sysprof_visualizer_size_allocate (GtkWidget     *widget,
                                  GtkAllocation *alloc)
{
  SysprofVisualizer *self = (SysprofVisualizer *)widget;
  SysprofVisualizerPrivate *priv = sysprof_visualizer_get_instance_private (self);

  g_assert (SYSPROF_IS_VISUALIZER (self));

  GTK_WIDGET_CLASS (sysprof_visualizer_parent_class)->size_allocate (widget, alloc);

  priv->cache_alloc = *alloc;
  adjust_alloc_for_borders (self, &priv->cache_alloc);
}

static void
sysprof_visualizer_finalize (GObject *object)
{
  SysprofVisualizer *self = (SysprofVisualizer *)object;
  SysprofVisualizerPrivate *priv = sysprof_visualizer_get_instance_private (self);

  g_clear_pointer (&priv->title, g_free);

  G_OBJECT_CLASS (sysprof_visualizer_parent_class)->finalize (object);
}

static void
sysprof_visualizer_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  SysprofVisualizer *self = SYSPROF_VISUALIZER (object);

  switch (prop_id)
    {
    case PROP_TITLE:
      g_value_set_string (value, sysprof_visualizer_get_title (self));
      break;

    case PROP_BEGIN_TIME:
      g_value_set_int64 (value, sysprof_visualizer_get_begin_time (self));
      break;

    case PROP_END_TIME:
      g_value_set_int64 (value, sysprof_visualizer_get_end_time (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_visualizer_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  SysprofVisualizer *self = SYSPROF_VISUALIZER (object);
  SysprofVisualizerPrivate *priv = sysprof_visualizer_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_TITLE:
      sysprof_visualizer_set_title (self, g_value_get_string (value));
      break;

    case PROP_BEGIN_TIME:
      priv->begin_time = g_value_get_int64 (value);
      priv->duration = priv->end_time - priv->begin_time;
      break;

    case PROP_END_TIME:
      priv->end_time = g_value_get_int64 (value);
      priv->duration = priv->end_time - priv->begin_time;
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_visualizer_class_init (SysprofVisualizerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = sysprof_visualizer_finalize;
  object_class->get_property = sysprof_visualizer_get_property;
  object_class->set_property = sysprof_visualizer_set_property;

  widget_class->size_allocate = sysprof_visualizer_size_allocate;

  properties [PROP_BEGIN_TIME] =
    g_param_spec_int64 ("begin-time",
                        "Begin Time",
                        "Begin Time",
                        G_MININT64,
                        G_MAXINT64,
                        0,
                        (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_END_TIME] =
    g_param_spec_int64 ("end-time",
                        "End Time",
                        "End Time",
                        G_MININT64,
                        G_MAXINT64,
                        0,
                        (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_TITLE] =
    g_param_spec_string ("title",
                         "Title",
                         "The title for the row",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_css_name (widget_class, "SysprofVisualizer");
}

static void
sysprof_visualizer_init (SysprofVisualizer *self)
{
}

const gchar *
sysprof_visualizer_get_title (SysprofVisualizer *self)
{
  SysprofVisualizerPrivate *priv = sysprof_visualizer_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_VISUALIZER (self), 0);

  return priv->title;
}

void
sysprof_visualizer_set_title (SysprofVisualizer *self,
                              const gchar       *title)
{
  SysprofVisualizerPrivate *priv = sysprof_visualizer_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_VISUALIZER (self));

  if (g_strcmp0 (priv->title, title) != 0)
    {
      g_free (priv->title);
      priv->title = g_strdup (title);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_TITLE]);
    }
}

gint64
sysprof_visualizer_get_begin_time (SysprofVisualizer *self)
{
  SysprofVisualizerPrivate *priv = sysprof_visualizer_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_VISUALIZER (self), 0);

  return priv->begin_time;
}

gint64
sysprof_visualizer_get_end_time (SysprofVisualizer *self)
{
  SysprofVisualizerPrivate *priv = sysprof_visualizer_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_VISUALIZER (self), 0);

  return priv->end_time;
}

gint64
sysprof_visualizer_get_duration (SysprofVisualizer *self)
{
  g_return_val_if_fail (SYSPROF_IS_VISUALIZER (self), 0);

  return sysprof_visualizer_get_end_time (self) -
         sysprof_visualizer_get_begin_time (self);
}

void
sysprof_visualizer_set_reader (SysprofVisualizer    *self,
                               SysprofCaptureReader *reader)
{
  SysprofVisualizerPrivate *priv = sysprof_visualizer_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_VISUALIZER (self));
  g_return_if_fail (reader != NULL);

  if (priv->begin_time == 0 || priv->end_time == 0)
    {
      priv->begin_time = sysprof_capture_reader_get_start_time (reader);
      priv->end_time = sysprof_capture_reader_get_end_time (reader);
      priv->duration = priv->end_time - priv->begin_time;
    }

  if (SYSPROF_VISUALIZER_GET_CLASS (self)->set_reader)
    SYSPROF_VISUALIZER_GET_CLASS (self)->set_reader (self, reader);

  gtk_widget_queue_allocate (GTK_WIDGET (self));
}

void
sysprof_visualizer_translate_points (SysprofVisualizer                    *self,
                                     const SysprofVisualizerRelativePoint *in_points,
                                     guint                                 n_in_points,
                                     SysprofVisualizerAbsolutePoint       *out_points,
                                     guint                                 n_out_points)
{
  SysprofVisualizerPrivate *priv = sysprof_visualizer_get_instance_private (self);
  const GtkAllocation *a;

  g_return_if_fail (SYSPROF_IS_VISUALIZER (self));
  g_return_if_fail (in_points != NULL);
  g_return_if_fail (out_points != NULL);
  g_return_if_fail (n_in_points == n_out_points);

  a = &priv->cache_alloc;

  for (guint i = 0; i < n_in_points; i++)
    {
      out_points[i].x = (in_points[i].x * a->width);
      out_points[i].y = a->height - (ABS (in_points[i].y) * a->height);
    }
}

gint
sysprof_visualizer_get_x_for_time (SysprofVisualizer *self,
                                   gint64             time)
{
  SysprofVisualizerPrivate *priv = sysprof_visualizer_get_instance_private (self);

  return ((time - priv->begin_time) / (gdouble)priv->duration) * priv->cache_alloc.width;
}

void
sysprof_visualizer_set_time_range (SysprofVisualizer *self,
                                   gint64             begin_time,
                                   gint64             end_time)
{
  SysprofVisualizerPrivate *priv = sysprof_visualizer_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_VISUALIZER (self));

  priv->begin_time = begin_time;
  priv->end_time = end_time;
  priv->duration = end_time - begin_time;

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_BEGIN_TIME]);
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_END_TIME]);

  gtk_widget_queue_allocate (GTK_WIDGET (self));
}
