/* sysprof-mark-visualizer.c
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

#define G_LOG_DOMAIN "sysprof-mark-visualizer"

#include "config.h"

#include "sysprof-mark-visualizer.h"

#define RECT_HEIGHT    (4)
#define RECT_MIN_WIDTH (3)
#define RECT_OVERLAP   (-1)

struct _SysprofMarkVisualizer
{
  SysprofVisualizer  parent_instance;
  GHashTable        *spans_by_group;
  GHashTable        *rgba_by_group;
  GHashTable        *rgba_by_kind;
  GHashTable        *row_by_kind;
  guint              x_is_dirty : 1;
};

G_DEFINE_TYPE (SysprofMarkVisualizer, sysprof_mark_visualizer, SYSPROF_TYPE_VISUALIZER)

static void
reset_positions (SysprofMarkVisualizer *self)
{
  g_assert (SYSPROF_IS_MARK_VISUALIZER (self));

  self->x_is_dirty = TRUE;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

SysprofVisualizer *
sysprof_mark_visualizer_new (GHashTable *groups)
{
  SysprofMarkVisualizer *self;
  guint n_items;
  gint height;

  g_return_val_if_fail (groups != NULL, NULL);

  self = g_object_new (SYSPROF_TYPE_MARK_VISUALIZER, NULL);
  self->spans_by_group = g_hash_table_ref (groups);

  reset_positions (self);

  n_items = g_hash_table_size (groups);
  height = MAX (35, n_items * (RECT_HEIGHT - RECT_OVERLAP));
  gtk_widget_set_size_request (GTK_WIDGET (self), -1, height);

  return SYSPROF_VISUALIZER (g_steal_pointer (&self));
}

static gboolean
sysprof_mark_visualizer_draw (GtkWidget *widget,
                              cairo_t   *cr)
{
  SysprofMarkVisualizer *self = (SysprofMarkVisualizer *)widget;
  SysprofVisualizer *vis = (SysprofVisualizer *)widget;
  GHashTableIter iter;
  GtkAllocation alloc;
  gpointer k, v;
  gboolean ret;
  gint n_groups = 0;
  gint y = 0;

  g_assert (SYSPROF_IS_MARK_VISUALIZER (self));
  g_assert (cr != NULL);

  ret = GTK_WIDGET_CLASS (sysprof_mark_visualizer_parent_class)->draw (widget, cr);
  if (self->spans_by_group == NULL)
    return ret;

  gtk_widget_get_allocation (widget, &alloc);

  /* Pre-calculate all time slots so we can join later */
  if (self->x_is_dirty)
    {
      g_hash_table_iter_init (&iter, self->spans_by_group);
      while (g_hash_table_iter_next (&iter, &k, &v))
        {
          const GArray *spans = v;

          for (guint i = 0; i < spans->len; i++)
            {
              SysprofMarkTimeSpan *span = &g_array_index (spans, SysprofMarkTimeSpan, i);

              span->x = sysprof_visualizer_get_x_for_time (vis, span->begin);
              span->x2 = sysprof_visualizer_get_x_for_time (vis, span->end);
            }
        }

      self->x_is_dirty = FALSE;
    }

  n_groups = g_hash_table_size (self->spans_by_group);

  g_hash_table_iter_init (&iter, self->spans_by_group);
  while (g_hash_table_iter_next (&iter, &k, &v))
    {
      static const GdkRGBA black = {0, 0, 0, 1};
      SysprofMarkTimeSpan *span;
      const gchar *group = k;
      const GArray *spans = v;
      const GdkRGBA *rgba;
      const GdkRGBA *kindrgba;
      const GdkRGBA *grouprgba;

      if ((grouprgba = g_hash_table_lookup (self->rgba_by_group, group)))
        {
          rgba = grouprgba;
          gdk_cairo_set_source_rgba (cr, rgba);
        }

      for (guint i = 0; i < spans->len; i++)
        {
          gint x1, x2;

          span = &g_array_index (spans, SysprofMarkTimeSpan, i);

          if (n_groups == 1)
            {
              rgba = &black;
              if ((kindrgba = g_hash_table_lookup (self->rgba_by_kind, GUINT_TO_POINTER (span->kind))))
                rgba = kindrgba;
              else if ((grouprgba = g_hash_table_lookup (self->rgba_by_group, group)))
                rgba = grouprgba;
              gdk_cairo_set_source_rgba (cr, rgba);
            }

          x1 = span->x;
          x2 = x1 + RECT_MIN_WIDTH;

          if (span->x2 > x2)
            x2 = span->x2;

          /* If we are limited to one group, we might need to get the row
           * height for the kind of span this is.
           */
          if (n_groups == 1)
            {
              gint row = GPOINTER_TO_INT (g_hash_table_lookup (self->row_by_kind, GUINT_TO_POINTER (span->kind)));
              y = row * (RECT_HEIGHT - RECT_OVERLAP);
            }

          for (guint j = i + 1; j < spans->len; j++)
            {
              const SysprofMarkTimeSpan *next = &g_array_index (spans, SysprofMarkTimeSpan, j);

              /* Don't join this if we are about to draw a different kind */
              if (n_groups == 1 && next->kind != span->kind)
                break;

              if (next->x <= x2)
                {
                  x2 = MAX (x2, next->x2);
                  i++;
                  continue;
                }

              break;
            }

          cairo_rectangle (cr, x1, y, x2 - x1, RECT_HEIGHT);

          if (n_groups == 1)
            cairo_fill (cr);
        }

      if (n_groups > 1)
        cairo_fill (cr);

      y += RECT_HEIGHT + RECT_OVERLAP;
    }

  return ret;
}

static void
sysprof_mark_visualizer_size_allocate (GtkWidget     *widget,
                                       GtkAllocation *alloc)
{
  SysprofMarkVisualizer *self = (SysprofMarkVisualizer *)widget;

  g_assert (SYSPROF_IS_MARK_VISUALIZER (self));
  g_assert (alloc != NULL);

  GTK_WIDGET_CLASS (sysprof_mark_visualizer_parent_class)->size_allocate (widget, alloc);

  reset_positions (self);
}

static void
sysprof_mark_visualizer_finalize (GObject *object)
{
  SysprofMarkVisualizer *self = (SysprofMarkVisualizer *)object;

  g_clear_pointer (&self->spans_by_group, g_hash_table_unref);
  g_clear_pointer (&self->rgba_by_group, g_hash_table_unref);
  g_clear_pointer (&self->rgba_by_kind, g_hash_table_unref);
  g_clear_pointer (&self->row_by_kind, g_hash_table_unref);

  G_OBJECT_CLASS (sysprof_mark_visualizer_parent_class)->finalize (object);
}

static void
sysprof_mark_visualizer_class_init (SysprofMarkVisualizerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = sysprof_mark_visualizer_finalize;

  widget_class->draw = sysprof_mark_visualizer_draw;
  widget_class->size_allocate = sysprof_mark_visualizer_size_allocate;
}

static void
sysprof_mark_visualizer_init (SysprofMarkVisualizer *self)
{
  self->rgba_by_kind = g_hash_table_new_full (NULL, NULL, NULL, g_free);
  self->row_by_kind = g_hash_table_new (NULL, NULL);
  self->rgba_by_group = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

void
sysprof_mark_visualizer_set_group_rgba (SysprofMarkVisualizer *self,
                                        const gchar           *group,
                                        const GdkRGBA         *rgba)
{
  g_return_if_fail (SYSPROF_IS_MARK_VISUALIZER (self));
  g_return_if_fail (group != NULL);

  g_hash_table_insert (self->rgba_by_group,
                       g_strdup (group),
                       g_memdup2 (rgba, sizeof *rgba));
}

void
sysprof_mark_visualizer_set_kind_rgba (SysprofMarkVisualizer *self,
                                       GHashTable            *rgba_by_kind)
{
  g_return_if_fail (SYSPROF_IS_MARK_VISUALIZER (self));

  if (rgba_by_kind != self->rgba_by_kind)
    {
      g_hash_table_remove_all (self->row_by_kind);

      g_clear_pointer (&self->rgba_by_kind, g_hash_table_unref);

      if (rgba_by_kind)
        {
          GHashTableIter iter;
          guint row = 0;
          gpointer k;

          self->rgba_by_kind = g_hash_table_ref (rgba_by_kind);

          g_hash_table_iter_init (&iter, rgba_by_kind);
          while (g_hash_table_iter_next (&iter, &k, NULL))
            g_hash_table_insert (self->row_by_kind, k, GUINT_TO_POINTER (row++));

          gtk_widget_set_size_request (GTK_WIDGET (self),
                                       -1,
                                       MAX (35, row * (RECT_HEIGHT - RECT_OVERLAP)));
        }
    }
}
