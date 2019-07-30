/* sysprof-scrollmap.c
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

#define G_LOG_DOMAIN "sysprof-scrollmap"

#include "config.h"

#include "sysprof-scrollmap.h"

#define BOX_SIZE 4

struct _SysprofScrollmap
{
  GtkScrollbar  parent_instance;

  gint64        begin_time;
  gint64        end_time;

  GArray       *timings;
  GArray       *buckets;
  GCancellable *cancellable;

  gint          most;
};

typedef struct
{
  gint64  begin_time;
  gint64  end_time;
  GArray *timings;
  gint    width;
  gint    height;
} Recalculate;

G_DEFINE_TYPE (SysprofScrollmap, sysprof_scrollmap, GTK_TYPE_SCROLLBAR)

static void
recalculate_free (gpointer data)
{
  Recalculate *state = data;

  g_clear_pointer (&state->timings, g_array_unref);
  g_slice_free (Recalculate, state);
}

static void
sysprof_scrollmap_recalculate_worker (GTask        *task,
                                      gpointer      source_object,
                                      gpointer      task_data,
                                      GCancellable *cancellable)
{
  Recalculate *state = task_data;
  g_autoptr(GArray) buckets = NULL;
  gint64 duration;
  gint n_buckets;

  g_assert (G_IS_TASK (task));
  g_assert (SYSPROF_IS_SCROLLMAP (source_object));
  g_assert (state != NULL);
  g_assert (state->timings != NULL);
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  duration = state->end_time - state->begin_time;
  n_buckets = MAX (10, state->width / (BOX_SIZE + 1));
  buckets = g_array_sized_new (FALSE, TRUE, sizeof (gint), n_buckets);
  g_array_set_size (buckets, n_buckets);

  for (guint i = 0; i < state->timings->len; i++)
    {
      gint64 t = g_array_index (state->timings, gint64, i);
      gint n;

      if (t < state->begin_time || t > state->end_time)
        continue;

      n = MIN (n_buckets - 1, ((t - state->begin_time) / (gdouble)duration) * n_buckets);

      g_assert (n < n_buckets);

      g_array_index (buckets, gint, n)++;
    }

  g_task_return_pointer (task,
                         g_steal_pointer (&buckets),
                         (GDestroyNotify) g_array_unref);
}

static void
sysprof_scrollmap_recalculate_async (SysprofScrollmap    *self,
                                     GCancellable        *cancellable,
                                     GAsyncReadyCallback  callback,
                                     gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  GtkAllocation alloc;
  Recalculate state;

  g_assert (SYSPROF_IS_SCROLLMAP (self));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_scrollmap_recalculate_async);

  if (self->timings == NULL)
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_CANCELLED,
                               "The operation was cancelled");
      return;
    }

  gtk_widget_get_allocation (GTK_WIDGET (self), &alloc);

  state.begin_time = self->begin_time;
  state.end_time = self->end_time;
  state.width = alloc.width;
  state.height = alloc.height;
  state.timings = g_array_ref (self->timings);

  g_task_set_task_data (task,
                        g_slice_dup (Recalculate, &state),
                        recalculate_free);
  g_task_run_in_thread (task, sysprof_scrollmap_recalculate_worker);
}

static GArray *
sysprof_scrollmap_recalculate_finish (SysprofScrollmap  *self,
                                      GAsyncResult      *result,
                                      GError           **error)
{
  g_assert (SYSPROF_IS_SCROLLMAP (self));
  g_assert (G_IS_TASK (result));

  return g_task_propagate_pointer (G_TASK (result), error);
}

static void
draw_boxes (const GtkAllocation *alloc,
            cairo_t             *cr,
            gint                 x,
            gint                 n_boxes)
{
  gint y;

  g_assert (cr != NULL);

  y = alloc->height - BOX_SIZE;

  for (gint i = 0; i < n_boxes; i++)
    {
      cairo_rectangle (cr, x, y, BOX_SIZE, -BOX_SIZE);
      y -= (BOX_SIZE + 1);
    }

  cairo_fill (cr);
}

static gboolean
sysprof_scrollmap_draw (GtkWidget *widget,
                        cairo_t   *cr)
{
  SysprofScrollmap *self = (SysprofScrollmap *)widget;
  GtkStyleContext *style_context;
  GtkAllocation alloc;
  GdkRGBA color;
  gint max_boxes;

  g_assert (SYSPROF_IS_SCROLLMAP (self));
  g_assert (cr != NULL);

  if (self->buckets == NULL)
    goto chainup;

  gtk_widget_get_allocation (widget, &alloc);
  max_boxes = alloc.height / (BOX_SIZE + 1) - 1;

  style_context = gtk_widget_get_style_context (widget);
  gtk_style_context_get_color (style_context,
                               gtk_style_context_get_state (style_context),
                               &color);
  gdk_cairo_set_source_rgba (cr, &color);

  for (guint i = 0; i < self->buckets->len; i++)
    {
      gint n = g_array_index (self->buckets, gint, i);
      gint x = 1 + i * (BOX_SIZE + 1);
      gint b = max_boxes * (n / (gdouble)self->most);

#if 1
      if (n > 0)
        b = MAX (b, 1);
#endif

      draw_boxes (&alloc, cr, x, b);
    }

chainup:
  return GTK_WIDGET_CLASS (sysprof_scrollmap_parent_class)->draw (widget, cr);
}

static void
sysprof_scrollmap_recalculate_cb (GObject      *object,
                                  GAsyncResult *result,
                                  gpointer      user_data)
{
  SysprofScrollmap *self = (SysprofScrollmap *)object;
  g_autoptr(GArray) buckets = NULL;

  g_assert (SYSPROF_IS_SCROLLMAP (self));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (user_data == NULL);

  if ((buckets = sysprof_scrollmap_recalculate_finish (self, result, NULL)))
    {
      self->most = 0;

      for (guint i = 0; i < buckets->len; i++)
        {
          gint n = g_array_index (buckets, gint, i);
          self->most = MAX (self->most, n);
        }

      g_clear_pointer (&self->buckets, g_array_unref);
      self->buckets = g_steal_pointer (&buckets);

      gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}

static void
sysprof_scrollmap_finalize (GObject *object)
{
  SysprofScrollmap *self = (SysprofScrollmap *)object;

  g_clear_pointer (&self->buckets, g_array_unref);
  g_clear_pointer (&self->timings, g_array_unref);

  G_OBJECT_CLASS (sysprof_scrollmap_parent_class)->finalize (object);
}

static void
sysprof_scrollmap_class_init (SysprofScrollmapClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = sysprof_scrollmap_finalize;

  widget_class->draw = sysprof_scrollmap_draw;
}

static void
sysprof_scrollmap_init (SysprofScrollmap *self)
{
}

void
sysprof_scrollmap_set_timings (SysprofScrollmap *self,
                               GArray           *timings)
{
  g_return_if_fail (SYSPROF_IS_SCROLLMAP (self));

  if (timings != self->timings)
    {
      g_clear_pointer (&self->timings, g_array_unref);
      self->timings = timings ? g_array_ref (timings) : NULL;
    }
}

void
sysprof_scrollmap_set_time_range (SysprofScrollmap *self,
                                  gint64            begin_time,
                                  gint64            end_time)
{
  g_return_if_fail (SYSPROF_IS_SCROLLMAP (self));

  self->begin_time = begin_time;
  self->end_time = end_time;

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);
  self->cancellable = g_cancellable_new ();

  sysprof_scrollmap_recalculate_async (self,
                                       self->cancellable,
                                       sysprof_scrollmap_recalculate_cb,
                                       NULL);
}
