/* rectangles.c
 *
 * Copyright 2018-2019 Christian Hergert <chergert@redhat.com>
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

#include "rectangles.h"
#include "sysprof-color-cycle.h"
#include "sysprof-visualizer.h"

typedef struct
{
  const gchar *name;
  const gchar *message;
  gint64 begin;
  gint64 end;
  GdkRectangle area;
} Rectangle;

struct _Rectangles
{
  GStringChunk *strings;
  GArray *rectangles;
  GHashTable *y_indexes;
  GHashTable *colors;
  SysprofColorCycle *cycle;
  gint64 begin_time;
  gint64 end_time;
  guint sorted : 1;
};

Rectangles *
rectangles_new (gint64 begin_time,
                gint64 end_time)
{
  Rectangles *self;

  self = g_slice_new0 (Rectangles);
  self->strings = g_string_chunk_new (4096);
  self->rectangles = g_array_new (FALSE, FALSE, sizeof (Rectangle));
  self->y_indexes = g_hash_table_new (g_str_hash, g_str_equal);
  self->colors = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
  self->cycle = sysprof_color_cycle_new ();
  self->begin_time = begin_time;
  self->end_time = end_time;
  self->sorted = FALSE;

  return self;
}

void
rectangles_add (Rectangles  *self,
                gint64       begin_time,
                gint64       end_time,
                const gchar *name,
                const gchar *message)
{
  Rectangle rect = {0};

  g_assert (self != NULL);

  if (message != NULL)
    rect.message = g_string_chunk_insert_const (self->strings, message);

  if (name != NULL)
    rect.name = g_string_chunk_insert_const (self->strings, name);

  rect.begin = begin_time;
  rect.end = end_time;

  if (rect.end == rect.begin)
    rect.area.width = 1;

  g_array_append_val (self->rectangles, rect);

  self->sorted = FALSE;
}

void
rectangles_free (Rectangles *self)
{
  g_string_chunk_free (self->strings);
  g_array_unref (self->rectangles);
  g_hash_table_unref (self->colors);
  g_hash_table_unref (self->y_indexes);
  sysprof_color_cycle_unref (self->cycle);
  g_slice_free (Rectangles, self);
}

static gint
sort_rectangles (gconstpointer a,
                 gconstpointer b)
{
  const Rectangle *r1 = a;
  const Rectangle *r2 = b;
  gint64 r = r1->begin - r2->begin;
  if (r == 0)
    r = r1->end - r2->end;
  if (r > 0) return 1;
  else if (r < 0) return -1;
  else return 0;
}

static void
rectangles_sort (Rectangles *self)
{
  guint sequence = 0;

  g_assert (self != NULL);

  if (self->sorted)
    return;

  g_array_sort (self->rectangles, sort_rectangles);

  g_hash_table_remove_all (self->y_indexes);

  for (guint i = 0; i < self->rectangles->len; i++)
    {
      const Rectangle *rect = &g_array_index (self->rectangles, Rectangle, i);

      if (!g_hash_table_contains (self->y_indexes, rect->name))
        {
          GdkRGBA rgba;

          sysprof_color_cycle_next (self->cycle, &rgba);
          g_hash_table_insert (self->y_indexes, (gchar *)rect->name, GUINT_TO_POINTER (++sequence));
          g_hash_table_insert (self->colors, (gchar *)rect->name, g_memdup2 (&rgba, sizeof rgba));
        }
    }

  self->sorted = TRUE;
}

void
rectangles_draw (Rectangles *self,
                 GtkWidget  *row,
                 cairo_t    *cr)
{
  GtkAllocation alloc;
  gdouble range;
  guint ns;

  g_assert (self != NULL);
  g_assert (SYSPROF_IS_VISUALIZER (row));
  g_assert (cr != NULL);

  if (!self->sorted)
    rectangles_sort (self);

  gtk_widget_get_allocation (row, &alloc);
  ns = g_hash_table_size (self->y_indexes);
  if (ns == 0 || alloc.height == 0)
    return;

  range = self->end_time - self->begin_time;

  for (guint i = 0; i < self->rectangles->len; i++)
    {
      Rectangle *rect = &g_array_index (self->rectangles, Rectangle, i);
      guint y_index = GPOINTER_TO_UINT (g_hash_table_lookup (self->y_indexes, rect->name));
      SysprofVisualizerRelativePoint in_points[2];
      SysprofVisualizerAbsolutePoint out_points[2];
      GdkRectangle r;
      GdkRGBA *rgba;

      g_assert (y_index > 0);
      g_assert (y_index <= ns);

      in_points[0].x = (rect->begin - self->begin_time) / range;
      in_points[0].y = (y_index - 1) / (gdouble)ns;
      in_points[1].x = (rect->end - self->begin_time) / range;
      in_points[1].y = 0;

      sysprof_visualizer_translate_points (SYSPROF_VISUALIZER (row),
                                               in_points, G_N_ELEMENTS (in_points),
                                               out_points, G_N_ELEMENTS (out_points));

      r.height = alloc.height / (gdouble)ns;
      r.x = out_points[0].x;
      r.y = out_points[0].y - r.height;

      if (rect->end <= rect->begin)
        r.width = 1;
      else
        r.width = MAX (1, out_points[1].x - out_points[0].x);

      rect->area = r;

      rgba = g_hash_table_lookup (self->colors, rect->name);

      gdk_cairo_rectangle (cr, &r);
      gdk_cairo_set_source_rgba (cr, rgba);
      cairo_fill (cr);
    }
}

void
rectangles_set_end_time (Rectangles *self,
                         gint64      end_time)
{
  /* We might not know the real end time until we've exhausted the stream */
  self->end_time = end_time;
}

gboolean
rectangles_query_tooltip (Rectangles  *self,
                          GtkTooltip  *tooltip,
                          const gchar *group,
                          gint         x,
                          gint         y)
{
  g_assert (self != NULL);
  g_assert (GTK_IS_TOOLTIP (tooltip));

  /* TODO: Sort, binary search, etc. */

  for (guint i = 0; i < self->rectangles->len; i++)
    {
      const Rectangle *r = &g_array_index (self->rectangles, Rectangle, i);

      if (r->area.x <= x &&
          r->area.y <= y &&
          r->area.x + r->area.width >= x &&
          r->area.y + r->area.height >= y)
        {
          g_autofree gchar *text = g_strdup_printf ("%s:%s: %s", group, r->name, r->message);
          gtk_tooltip_set_text (tooltip, text);
          return TRUE;
        }
    }

  return FALSE;
}
