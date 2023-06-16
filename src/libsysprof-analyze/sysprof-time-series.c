/* sysprof-time-series.c
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

#include "sysprof-time-series.h"

struct _SysprofTimeSeries
{
  /* Model of SysprofDocumentFrame */
  GListModel      *model;

  /* Array of SysprofTimeSeriesValue */
  GArray          *values;

  /* The timespan contained in this series */
  SysprofTimeSpan  time_span;

  /* The duration of the series */
  gint64           duration;

  /* Used for warning on items-changed */
  gulong           items_changed_handler;
};

G_DEFINE_BOXED_TYPE (SysprofTimeSeries,
                     sysprof_time_series,
                     sysprof_time_series_ref,
                     sysprof_time_series_unref)

static void
warn_on_items_changed_cb (GListModel *model,
                          guint       position,
                          guint       removed,
                          guint       added,
                          gpointer    user_data)
{
  g_critical ("%s @ %p emitted items changed while a timeseries is active! Expect errors.",
              G_OBJECT_TYPE_NAME (model), model);
}

/**
 * sysprof_time_series_new:
 * @model: a #GListModel
 * @time_span: the span of the time series
 *
 * Used for creating normalized time spans (between 0..1) for times
 * within a timespan. Useful for creating time-based charts at any
 * size or scale.
 *
 * It is required that @model does not change during the lifetime
 * of the time series.
 */
SysprofTimeSeries *
sysprof_time_series_new (GListModel      *model,
                         SysprofTimeSpan  time_span)
{
  SysprofTimeSeries *self;

  g_return_val_if_fail (G_IS_LIST_MODEL (model), NULL);

  self = g_atomic_rc_box_new0 (SysprofTimeSeries);
  self->model = g_object_ref (model);
  self->values = g_array_new (FALSE, FALSE, sizeof (SysprofTimeSeriesValue));
  self->time_span = sysprof_time_span_order (time_span);
  self->duration = MAX (1, self->time_span.end_nsec - self->time_span.begin_nsec);
  self->items_changed_handler =
    g_signal_connect (self->model,
                      "items-changed",
                      G_CALLBACK (warn_on_items_changed_cb),
                      NULL);

  return self;
}

SysprofTimeSeries *
sysprof_time_series_ref (SysprofTimeSeries *self)
{
  return g_atomic_rc_box_acquire (self);
}

static void
_sysprof_time_series_finalize (SysprofTimeSeries *self)
{
  g_clear_signal_handler (&self->items_changed_handler, self->model);
  g_clear_object (&self->model);
  g_clear_pointer (&self->values, g_array_unref);
}

void
sysprof_time_series_unref (SysprofTimeSeries *self)
{
  g_atomic_rc_box_release_full (self,
                                (GDestroyNotify)_sysprof_time_series_finalize);
}

void
sysprof_time_series_add (SysprofTimeSeries *self,
                         SysprofTimeSpan    time_span,
                         guint              index)
{
  SysprofTimeSeriesValue value;

  time_span = sysprof_time_span_order (time_span);

  if (!sysprof_time_span_clamp (&time_span, self->time_span))
    return;

  value.index = index;
  sysprof_time_span_normalize (time_span, self->time_span, value.time);

  g_array_append_val (self->values, value);
}

/**
 * sysprof_time_series_get_model:
 * @self: a #SysprofTimeSeries
 *
 * Gets the underlying model for the time series.
 *
 * Returns: (transfer none): a #GListModel
 */
GListModel *
sysprof_time_series_get_model (SysprofTimeSeries *self)
{
  g_return_val_if_fail (self != NULL, NULL);

  return self->model;
}

const SysprofTimeSeriesValue *
sysprof_time_series_get_values (const SysprofTimeSeries *self,
                                guint                   *n_values)
{
  *n_values = self->values->len;
  return &g_array_index (self->values, SysprofTimeSeriesValue, 0);
}

static int
compare_by_time (gconstpointer a,
                 gconstpointer b)
{
  const SysprofTimeSeriesValue *aval = a;
  const SysprofTimeSeriesValue *bval = b;

  if (aval->begin < bval->begin)
    return -1;
  else if (aval->begin > bval->begin)
    return 1;

  if (aval->end < bval->end)
    return -1;
  else if (aval->end > bval->end)
    return 1;

  return 0;
}

void
sysprof_time_series_sort (SysprofTimeSeries *self)
{
  qsort (self->values->data,
         self->values->len,
         sizeof (SysprofTimeSeriesValue),
         compare_by_time);
}
