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

#define NSEC_PER_DAY (SYSPROF_NSEC_PER_SEC * 60L * 60L * 24L)
#define NSEC_PER_HOUR (SYSPROF_NSEC_PER_SEC * 60L * 60L)
#define NSEC_PER_MIN (SYSPROF_NSEC_PER_SEC * 60L)
#define NSEC_PER_MSEC (SYSPROF_NSEC_PER_SEC/G_GINT64_CONSTANT(1000))
#define MIN_TICK_DISTANCE 20
#define LABEL_HEIGHT_PX 10

struct _SysprofVisualizerTicks
{
  SysprofVisualizer parent_instance;
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
  TICK_TEN_THOUSANDTHS,
  N_TICKS
};

struct {
  gint width;
  gint height;
  gint64 span;
} tick_sizing[N_TICKS] = {
  { 1, 12, SYSPROF_NSEC_PER_SEC * 60 },
  { 1, 11, SYSPROF_NSEC_PER_SEC * 30 },
  { 1, 10, SYSPROF_NSEC_PER_SEC * 5 },
  { 1, 9, SYSPROF_NSEC_PER_SEC },
  { 1, 8, SYSPROF_NSEC_PER_SEC / 2 },
  { 1, 6, SYSPROF_NSEC_PER_SEC / 4 },
  { 1, 5, SYSPROF_NSEC_PER_SEC / 10 },
  { 1, 4, SYSPROF_NSEC_PER_SEC / 100 },
  { 1, 3, SYSPROF_NSEC_PER_SEC / 1000 },
  { 1, 1, SYSPROF_NSEC_PER_SEC / 10000 },
};

G_DEFINE_TYPE (SysprofVisualizerTicks, sysprof_visualizer_ticks, SYSPROF_TYPE_VISUALIZER)

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

  tmp = time % SYSPROF_NSEC_PER_SEC;
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

  if (time >= SYSPROF_NSEC_PER_SEC)
    {
      sec = time / SYSPROF_NSEC_PER_SEC;
      time %= SYSPROF_NSEC_PER_SEC;
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

static gboolean
draw_ticks (SysprofVisualizerTicks *self,
            cairo_t           *cr,
            GtkAllocation     *area,
            gint               ticks,
            gboolean           label_mode)
{
  GtkAllocation alloc;
  gint64 begin_time, end_time;
  gdouble half;
  gint count = 0;

  g_assert (SYSPROF_IS_VISUALIZER_TICKS (self));
  g_assert (cr != NULL);
  g_assert (area != NULL);
  g_assert (ticks >= 0);
  g_assert (ticks < N_TICKS);

  begin_time = sysprof_visualizer_get_begin_time (SYSPROF_VISUALIZER (self));
  end_time = sysprof_visualizer_get_end_time (SYSPROF_VISUALIZER (self));

  /*
   * If we are in label_model, we don't draw the ticks but only the labels.
   * That way we can determine which tick level managed to draw a tick in
   * the visible region so we only show labels for the largest tick level.
   * (Returning true from this method indicates we successfully drew a tick).
   */

  half = tick_sizing[ticks].width / 2.0;

  gtk_widget_get_allocation (GTK_WIDGET (self), &alloc);

  if G_UNLIKELY (label_mode)
    {
      PangoFontDescription *font_desc;
      PangoLayout *layout;
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
      want_msec = tick_sizing[ticks].span < SYSPROF_NSEC_PER_SEC;

      for (gint64 t = begin_time; t <= end_time; t += tick_sizing[ticks].span)
        {
          gdouble x = sysprof_visualizer_get_x_for_time (SYSPROF_VISUALIZER (self), t);

          if (x < (last_x2 + MIN_TICK_DISTANCE))
            continue;

          cairo_move_to (cr, (gint)x + 2.5 - (gint)half, 2);
          update_label_text (layout, t - begin_time, want_msec);
          pango_layout_get_pixel_size (layout, &w, &h);

          if (x + w <= alloc.width)
            pango_cairo_show_layout (cr, layout);

          last_x2 = x + w;
        }

      g_clear_object (&layout);
    }
  else
    {
      for (gint64 t = begin_time; t <= end_time; t += tick_sizing[ticks].span)
        {
          gdouble x = sysprof_visualizer_get_x_for_time (SYSPROF_VISUALIZER (self), t);
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

  timespan = sysprof_visualizer_get_duration (SYSPROF_VISUALIZER (self));
  if (timespan == 0)
    return GDK_EVENT_PROPAGATE;

  style = gtk_widget_get_style_context (widget);

  gtk_widget_get_allocation (GTK_WIDGET (self), &alloc);
  alloc.x = 0;
  alloc.y = 0;

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
}

GtkWidget *
sysprof_visualizer_ticks_new (void)
{
  return g_object_new (SYSPROF_TYPE_VISUALIZER_TICKS, NULL);
}
