/* sp-visualizer-ticks.c
 *
 * Copyright (C) 2016 Christian Hergert <chergert@redhat.com>
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

#include <glib/gi18n.h>

#include "sp-visualizer-ticks.h"

#define NSEC_PER_SEC G_GINT64_CONSTANT(1000000000)
#define MIN_TICK_DISTANCE 20

struct _SpVisualizerTicks
{
  GtkDrawingArea parent_instance;

  gint64 begin_time;
  gint64 end_time;
};

enum {
  TICK_MINUTES,
  TICK_HALF_MINUTES,
  TICK_FIVE_SECONDS,
  TICK_SECONDS,
  TICK_HALF_SECONDS,
  TICK_QUARTER_SECONDS,
  TICK_TENTHS,
  TICK_HUNDREDTHS,
  TICK_THOUSANDTHS,
  N_TICKS
};

struct {
  gint width;
  gint height;
  gint64 span;
} tick_sizing[N_TICKS] = {
  { 3, 12, NSEC_PER_SEC * 60 },
  { 1, 11, NSEC_PER_SEC * 30 },
  { 1, 10, NSEC_PER_SEC * 5 },
  { 1, 9, NSEC_PER_SEC },
  { 1, 8, NSEC_PER_SEC / 2 },
  { 1, 6, NSEC_PER_SEC / 4 },
  { 1, 5, NSEC_PER_SEC / 10 },
  { 1, 4, NSEC_PER_SEC / 100 },
  { 1, 3, NSEC_PER_SEC / 1000 },
};

G_DEFINE_TYPE (SpVisualizerTicks, sp_visualizer_ticks, GTK_TYPE_DRAWING_AREA)

static void
draw_ticks (SpVisualizerTicks *self,
            cairo_t           *cr,
            GtkAllocation     *area,
            gint               ticks)
{
  gdouble half;
  gdouble space;
  gint64 timespan;

  g_assert (SP_IS_VISUALIZER_TICKS (self));
  g_assert (cr != NULL);
  g_assert (area != NULL);
  g_assert (ticks >= 0);
  g_assert (ticks < N_TICKS);

  timespan = self->end_time - self->begin_time;
  space = (gdouble)area->width / (gdouble)timespan * (gdouble)tick_sizing[ticks].span;
  half = tick_sizing[ticks].width / 2.0;

  g_assert (space >= MIN_TICK_DISTANCE);

  for (gdouble x = 0; x < area->width; x += space)
    {
      cairo_move_to (cr, (gint)x - half, 0);
      cairo_line_to (cr, (gint)x - half, tick_sizing[ticks].height);
    }

  cairo_set_line_width (cr, tick_sizing[ticks].width);
  cairo_stroke (cr);
}

static gboolean
sp_visualizer_ticks_draw (GtkWidget *widget,
                          cairo_t   *cr)
{
  SpVisualizerTicks *self = (SpVisualizerTicks *)widget;
  GtkAllocation alloc;
  gint64 timespan;

  g_assert (SP_IS_VISUALIZER_TICKS (self));
  g_assert (cr != NULL);

  if (0 == (timespan = self->end_time - self->begin_time))
    return GDK_EVENT_PROPAGATE;

  gtk_widget_get_allocation (GTK_WIDGET (self), &alloc);

  cairo_set_source_rgba (cr, 0, 0, 0, .2);

  /*
   * We need to discover up to what level we will draw tick marks.
   * This is based on the width of the widget and the number of ticks
   * to draw (which is determined from our timespan). We will skip a
   * mark if they will end up less than MIN_TICK_DISTANCE px apart.
   */

  for (guint i = G_N_ELEMENTS (tick_sizing); i > 0; i--)
    {
      gint64 n_ticks = timespan / tick_sizing[i - 1].span;

      if ((alloc.width / n_ticks) < MIN_TICK_DISTANCE)
        continue;

      for (guint j = 0; j < i; j++)
        draw_ticks (self, cr, &alloc, j);

      break;
    }

  return GDK_EVENT_PROPAGATE;
}

static void
sp_visualizer_ticks_get_preferred_height (GtkWidget *widget,
                                          gint      *min_height,
                                          gint      *nat_height)
{
  g_assert (SP_IS_VISUALIZER_TICKS (widget));

  *min_height = *nat_height = tick_sizing[0].height;
}

static void
sp_visualizer_ticks_class_init (SpVisualizerTicksClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  widget_class->draw = sp_visualizer_ticks_draw;
  widget_class->get_preferred_height = sp_visualizer_ticks_get_preferred_height;
}

static void
sp_visualizer_ticks_init (SpVisualizerTicks *self)
{
  /* XXX: set range from callers */
  self->end_time = 1000000000UL * 60UL;
}

GtkWidget *
sp_visualizer_ticks_new (void)
{
  return g_object_new (SP_TYPE_VISUALIZER_TICKS, NULL);
}

void
sp_visualizer_ticks_get_time_range (SpVisualizerTicks *self,
                                    gint64            *begin_time,
                                    gint64            *end_time)
{
  g_return_if_fail (SP_IS_VISUALIZER_TICKS (self));
  g_return_if_fail (begin_time != NULL || end_time != NULL);

  if (begin_time != NULL)
    *begin_time = self->begin_time;

  if (end_time != NULL)
    *end_time = self->end_time;
}

void
sp_visualizer_ticks_set_time_range (SpVisualizerTicks *self,
                                    gint64             begin_time,
                                    gint64             end_time)
{
  g_return_if_fail (SP_IS_VISUALIZER_TICKS (self));

  if (begin_time < end_time)
    {
      gint64 tmp = begin_time;
      begin_time = end_time;
      end_time = tmp;
    }

  self->begin_time = begin_time;
  self->end_time = end_time;

  gtk_widget_queue_draw (GTK_WIDGET (self));
}
