/* sysprof-xy-series.c
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

#include "sysprof-xy-series.h"

struct _SysprofXYSeries
{
  /* Model of SysprofDocumentFrame */
  GListModel      *model;

  /* Array of SysprofXYSeriesValue */
  GArray          *values;

  /* Our bounds for non-normalized values */
  float            min_x;
  float            min_y;
  float            max_x;
  float            max_y;

  /* Pre-calculated distance between min/max */
  float            x_distance;
  float            y_distance;

  /* Used for warning on items-changed */
  gulong           items_changed_handler;
};

G_DEFINE_BOXED_TYPE (SysprofXYSeries,
                     sysprof_xy_series,
                     sysprof_xy_series_ref,
                     sysprof_xy_series_unref)

static void
warn_on_items_changed_cb (GListModel *model,
                          guint       position,
                          guint       removed,
                          guint       added,
                          gpointer    user_data)
{
  g_critical ("%s @ %p emitted items changed while an XYSeries is active! Expect errors.",
              G_OBJECT_TYPE_NAME (model), model);
}

/**
 * sysprof_xy_series_new:
 * @model: a #GListModel
 * @xy_span: the span of the xy series
 *
 * Used for creating normalized xy spans (between 0..1) for xys
 * within a xyspan. Useful for creating xy-based charts at any
 * size or scale.
 *
 * It is required that @model does not change during the lifexy
 * of the xy series.
 */
SysprofXYSeries *
sysprof_xy_series_new (GListModel *model,
                       float       min_x,
                       float       min_y,
                       float       max_x,
                       float       max_y)
{
  SysprofXYSeries *self;

  g_return_val_if_fail (G_IS_LIST_MODEL (model), NULL);

  self = g_atomic_rc_box_new0 (SysprofXYSeries);
  self->model = g_object_ref (model);
  self->values = g_array_new (FALSE, FALSE, sizeof (SysprofXYSeriesValue));
  self->min_x = min_x;
  self->min_y = min_y;
  self->max_x = max_x;
  self->max_y = max_y;
  self->items_changed_handler =
    g_signal_connect (self->model,
                      "items-changed",
                      G_CALLBACK (warn_on_items_changed_cb),
                      NULL);

  return self;
}

SysprofXYSeries *
sysprof_xy_series_ref (SysprofXYSeries *self)
{
  return g_atomic_rc_box_acquire (self);
}

static void
_sysprof_xy_series_finalize (SysprofXYSeries *self)
{
  g_clear_signal_handler (&self->items_changed_handler, self->model);
  g_clear_object (&self->model);
  g_clear_pointer (&self->values, g_array_unref);
}

void
sysprof_xy_series_unref (SysprofXYSeries *self)
{
  g_atomic_rc_box_release_full (self,
                                (GDestroyNotify)_sysprof_xy_series_finalize);
}

void
sysprof_xy_series_add (SysprofXYSeries *self,
                       float            x,
                       float            y,
                       guint            index)
{
  SysprofXYSeriesValue value;

  if (x < self->min_x || x > self->max_x)
    return;

  if (y < self->min_y || y > self->max_y)
    return;

  value.x = (x - self->min_x) / self->x_distance;
  value.y = (y - self->min_y) / self->y_distance;
  value.index = index;

  g_array_append_val (self->values, value);
}

/**
 * sysprof_xy_series_get_model:
 * @self: a #SysprofXYSeries
 *
 * Gets the underlying model for the xy series.
 *
 * Returns: (transfer none): a #GListModel
 */
GListModel *
sysprof_xy_series_get_model (SysprofXYSeries *self)
{
  g_return_val_if_fail (self != NULL, NULL);

  return self->model;
}

const SysprofXYSeriesValue *
sysprof_xy_series_get_values (const SysprofXYSeries *self,
                                guint                   *n_values)
{
  *n_values = self->values->len;
  return &g_array_index (self->values, SysprofXYSeriesValue, 0);
}

static int
compare_by_xy (gconstpointer aptr,
               gconstpointer bptr)
{
  const SysprofXYSeriesValue *a = aptr;
  const SysprofXYSeriesValue *b = bptr;

  if (a->x < b->x)
    return -1;
  else if (a->x > b->x)
    return 1;

  if (a->y < b->y)
    return -1;
  else if (a->y > b->y)
    return 1;

  return 0;
}

void
sysprof_xy_series_sort (SysprofXYSeries *self)
{
  qsort (self->values->data,
         self->values->len,
         sizeof (SysprofXYSeriesValue),
         compare_by_xy);
}

void
sysprof_xy_series_get_range (SysprofXYSeries *self,
                             float           *min_x,
                             float           *min_y,
                             float           *max_x,
                             float           *max_y)
{
  g_return_if_fail (self != NULL);

  if (min_x)
    *min_x = self->min_x;

  if (max_x)
    *max_x = self->max_x;

  if (min_y)
    *min_y = self->min_y;

  if (max_y)
    *max_y = self->max_y;
}
