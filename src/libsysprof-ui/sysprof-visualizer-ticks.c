/* sysprof-visualizer-ticks.c
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

#define G_LOG_DOMAIN "sysprof-visualizer-ticks"

#include "config.h"

#include <glib/gi18n.h>
#include <sysprof.h>

#include "sysprof-visualizer-ticks.h"

#define NSEC_PER_SEC G_GINT64_CONSTANT(1000000000)
#define NSEC_PER_DAY (NSEC_PER_SEC * 60L * 60L * 24L)
#define NSEC_PER_HOUR (NSEC_PER_SEC * 60L * 60L)
#define NSEC_PER_MIN (NSEC_PER_SEC * 60L)
#define NSEC_PER_MSEC (NSEC_PER_SEC/G_GINT64_CONSTANT(1000))
#define MIN_TICK_DISTANCE 20
#define LABEL_HEIGHT_PX 10

SYSPROF_ALIGNED_BEGIN (8)
struct _SysprofVisualizerTicks
{
  GtkDrawingArea parent_instance;

  gint64 epoch;
  gint64 begin_time;
  gint64 end_time;
} SYSPROF_ALIGNED_END (8);

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
  TICK_TEN_THOUSANDTHS,
  N_TICKS
};

struct {
  gint width;
  gint height;
  gint64 span;
} tick_sizing[N_TICKS] = {
  { 1, 12, NSEC_PER_SEC * 60 },
  { 1, 11, NSEC_PER_SEC * 30 },
  { 1, 10, NSEC_PER_SEC * 5 },
  { 1, 9, NSEC_PER_SEC },
  { 1, 8, NSEC_PER_SEC / 2 },
  { 1, 6, NSEC_PER_SEC / 4 },
  { 1, 5, NSEC_PER_SEC / 10 },
  { 1, 4, NSEC_PER_SEC / 100 },
  { 1, 3, NSEC_PER_SEC / 1000 },
  { 1, 1, NSEC_PER_SEC / 10000 },
};

G_DEFINE_TYPE (SysprofVisualizerTicks, sysprof_visualizer_ticks, GTK_TYPE_DRAWING_AREA)

static void
update_label_text (PangoLayout *layout,
                   gint64       time,
                   gboolean     want_msec)
{
  g_autofree gchar *str = NULL;
  gint64 tmp;
  gint msec = 0;
  gint hours = 0;
  gint min = 0;
  gint sec = 0;
  G_GNUC_UNUSED gint days = 0;

  g_assert (PANGO_IS_LAYOUT (layout));

  tmp = time % NSEC_PER_SEC;
  time -= tmp;
  msec = tmp / 100000L;

  if (time >= NSEC_PER_DAY)
    {
      days = time / NSEC_PER_DAY;
      time %= NSEC_PER_DAY;
    }

  if (time >= NSEC_PER_HOUR)
    {
      hours = time / NSEC_PER_HOUR;
      time %= NSEC_PER_HOUR;
    }

  if (time >= NSEC_PER_MIN)
    {
      min = time / NSEC_PER_MIN;
      time %= NSEC_PER_MIN;
    }

  if (time >= NSEC_PER_SEC)
    {
      sec = time / NSEC_PER_SEC;
      time %= NSEC_PER_SEC;
    }

  if (want_msec || (!hours && !min && !sec && msec))
    {
      if (hours > 0)
        str = g_strdup_printf ("%02u:%02u:%02u.%04u", hours, min, sec, msec);
      else
        str = g_strdup_printf ("%02u:%02u.%04u", min, sec, msec);
    }
  else
    {
      if (hours > 0)
        str = g_strdup_printf ("%02u:%02u:%02u", hours, min, sec);
      else
        str = g_strdup_printf ("%02u:%02u", min, sec);
    }

  pango_layout_set_text (layout, str, -1);
}

static inline gdouble
get_x_for_time (SysprofVisualizerTicks   *self,
                const GtkAllocation *alloc,
                gint64               t)
{
  gint64 timespan = self->end_time - self->begin_time;
  gdouble x_ratio = (gdouble)(t - self->begin_time) / (gdouble)timespan;
  return alloc->width * x_ratio;
}

static gboolean
draw_ticks (SysprofVisualizerTicks *self,
            cairo_t           *cr,
            GtkAllocation     *area,
            gint               ticks,
            gboolean           label_mode)
{
  GtkAllocation alloc;
  gdouble half;
  gint64 x_offset;
  gint count = 0;

  g_assert (SYSPROF_IS_VISUALIZER_TICKS (self));
  g_assert (cr != NULL);
  g_assert (area != NULL);
  g_assert (ticks >= 0);
  g_assert (ticks < N_TICKS);

  /*
   * If we are in label_model, we don't draw the ticks but only the labels.
   * That way we can determine which tick level managed to draw a tick in
   * the visible region so we only show labels for the largest tick level.
   * (Returning true from this method indicates we successfully drew a tick).
   */

  half = tick_sizing[ticks].width / 2.0;

  /* Take our epoch into account to calculate the offset of the
   * first tick, which might not align perfectly when the beginning
   * of the visible area.
   */
  x_offset = (self->begin_time - self->epoch) % tick_sizing[ticks].span;
  gtk_widget_get_allocation (GTK_WIDGET (self), &alloc);

  if G_UNLIKELY (label_mode)
    {
      PangoLayout *layout;
      PangoFontDescription *font_desc;
      gboolean want_msec;
      gint last_x2 = G_MININT;
      gint w, h;

      layout = gtk_widget_create_pango_layout (GTK_WIDGET (self), "00:10:00.0000");

      font_desc = pango_font_description_new ();
      pango_font_description_set_family_static (font_desc, "Monospace");
      pango_font_description_set_absolute_size (font_desc, LABEL_HEIGHT_PX * PANGO_SCALE);
      pango_layout_set_font_description (layout, font_desc);
      pango_font_description_free (font_desc);

      pango_layout_get_pixel_size (layout, &w, &h);

      /* If we are operating on smaller than seconds here, then we want
       * to ensure we include msec with the timestamps.
       */
      want_msec = tick_sizing[ticks].span < NSEC_PER_SEC;

      for (gint64 t = self->begin_time - x_offset;
           t <= self->end_time;
           t += tick_sizing[ticks].span)
        {
          gdouble x = get_x_for_time (self, &alloc, t);

          if (x < (last_x2 + MIN_TICK_DISTANCE))
            continue;

          cairo_move_to (cr, (gint)x + 2.5 - (gint)half, 2);
          update_label_text (layout, t - self->epoch, want_msec);
          pango_layout_get_pixel_size (layout, &w, &h);
          pango_cairo_show_layout (cr, layout);

          last_x2 = x + w;
        }

      g_clear_object (&layout);
    }
  else
    {
      for (gint64 t = self->begin_time - x_offset;
           t <= self->end_time;
           t += tick_sizing[ticks].span)
        {
          gdouble x = get_x_for_time (self, &alloc, t);

          cairo_move_to (cr, (gint)x - .5 - (gint)half, alloc.height);
          cairo_line_to (cr, (gint)x - .5 - (gint)half, alloc.height - tick_sizing[ticks].height);
          count++;
        }

      cairo_set_line_width (cr, tick_sizing[ticks].width);
      cairo_stroke (cr);
    }

  return count > 2;
}

static gboolean
sysprof_visualizer_ticks_draw (GtkWidget *widget,
                               cairo_t   *cr)
{
  SysprofVisualizerTicks *self = SYSPROF_VISUALIZER_TICKS (widget);
  GtkStyleContext *style;
  GtkAllocation alloc;
  GtkStateFlags state;
  gint64 timespan;
  GdkRGBA color;

  g_assert (SYSPROF_IS_VISUALIZER_TICKS (self));
  g_assert (cr != NULL);

  if (0 == (timespan = self->end_time - self->begin_time))
    return GDK_EVENT_PROPAGATE;

  style = gtk_widget_get_style_context (widget);

  gtk_widget_get_allocation (GTK_WIDGET (self), &alloc);

  gtk_render_background (style, cr, 0, 0, alloc.width, alloc.height);

  state = gtk_widget_get_state_flags (widget);
  gtk_style_context_get_color (style, state, &color);

  gdk_cairo_set_source_rgba (cr, &color);

  /*
   * We need to discover up to what level we will draw tick marks.
   * This is based on the width of the widget and the number of ticks
   * to draw (which is determined from our timespan). We will skip a
   * mark if they will end up less than MIN_TICK_DISTANCE px apart.
   */

  for (guint i = G_N_ELEMENTS (tick_sizing); i > 0; i--)
    {
      gint64 n_ticks = timespan / tick_sizing[i - 1].span;
      gint largest_match = -1;

      if (n_ticks == 0 || (alloc.width / n_ticks) < MIN_TICK_DISTANCE)
        continue;

      for (guint j = i; j > 0; j--)
        {
          if (draw_ticks (self, cr, &alloc, j - 1, FALSE))
            largest_match = j - 1;
        }

      if (largest_match != -1)
        draw_ticks (self, cr, &alloc, largest_match, TRUE);

      break;
    }

  return GDK_EVENT_PROPAGATE;
}

static void
sysprof_visualizer_ticks_get_preferred_height (GtkWidget *widget,
                                               gint      *min_height,
                                               gint      *nat_height)
{
  g_assert (SYSPROF_IS_VISUALIZER_TICKS (widget));

  *min_height = *nat_height = tick_sizing[0].height + LABEL_HEIGHT_PX;
}

static void
sysprof_visualizer_ticks_class_init (SysprofVisualizerTicksClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  widget_class->draw = sysprof_visualizer_ticks_draw;
  widget_class->get_preferred_height = sysprof_visualizer_ticks_get_preferred_height;

  gtk_widget_class_set_css_name (widget_class, "SysprofVisualizerTicks");
}

static void
sysprof_visualizer_ticks_init (SysprofVisualizerTicks *self)
{
  self->end_time = G_GINT64_CONSTANT (1000000000) * 60;

  gtk_widget_set_has_window (GTK_WIDGET (self), FALSE);
}

GtkWidget *
sysprof_visualizer_ticks_new (void)
{
  return g_object_new (SYSPROF_TYPE_VISUALIZER_TICKS, NULL);
}

void
sysprof_visualizer_ticks_get_time_range (SysprofVisualizerTicks *self,
                                         gint64                 *begin_time,
                                         gint64                 *end_time)
{
  g_return_if_fail (SYSPROF_IS_VISUALIZER_TICKS (self));
  g_return_if_fail (begin_time != NULL || end_time != NULL);

  if (begin_time != NULL)
    *begin_time = self->begin_time;

  if (end_time != NULL)
    *end_time = self->end_time;
}

void
sysprof_visualizer_ticks_set_time_range (SysprofVisualizerTicks *self,
                                         gint64                  begin_time,
                                         gint64                  end_time)
{
  g_return_if_fail (SYSPROF_IS_VISUALIZER_TICKS (self));

  if (begin_time > end_time)
    {
      gint64 tmp = begin_time;
      begin_time = end_time;
      end_time = tmp;
    }

  self->begin_time = begin_time;
  self->end_time = end_time;

  gtk_widget_queue_draw (GTK_WIDGET (self));
}

gint64
sysprof_visualizer_ticks_get_epoch (SysprofVisualizerTicks *self)
{
  g_return_val_if_fail (SYSPROF_IS_VISUALIZER_TICKS (self), 0);

  return self->epoch;
}

/*
 * Sets the epoch for the visualizer ticks.
 *
 * The epoch is the "real" starting time of the capture, where as the
 * sysprof_visualizer_ticks_set_time_range() function sets the visible range
 * of the capture.
 *
 * This is used to calculate the offset of the beginning of the capture
 * from begin_time so that the ticks appear in the proper location.
 *
 * This function should only need to be called when the reader is changed.
 */
void
sysprof_visualizer_ticks_set_epoch (SysprofVisualizerTicks *self,
                               gint64             epoch)
{
  g_return_if_fail (SYSPROF_IS_VISUALIZER_TICKS (self));

  if (self->epoch != epoch)
    {
      self->epoch = epoch;
      gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}

gint64
sysprof_visualizer_ticks_get_duration (SysprofVisualizerTicks *self)
{
  g_return_val_if_fail (SYSPROF_IS_VISUALIZER_TICKS (self), 0);

  return self->end_time - self->begin_time;
}
