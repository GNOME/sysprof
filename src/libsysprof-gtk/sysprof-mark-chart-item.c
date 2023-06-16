/* sysprof-mark-chart-item.c
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

#include "sysprof-mark-chart-item-private.h"

struct _SysprofMarkChartItem
{
  GObject parent_instance;
  SysprofSession *session;
  SysprofMarkCatalog *catalog;
};

enum {
  PROP_0,
  PROP_SESSION,
  PROP_CATALOG,
  N_PROPS
};

enum {
  CHANGED,
  N_SIGNALS
};

G_DEFINE_FINAL_TYPE (SysprofMarkChartItem, sysprof_mark_chart_item, G_TYPE_OBJECT)

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

static void
sysprof_mark_chart_item_session_notify_selection_cb (SysprofMarkChartItem *self,
                                                     GParamSpec           *pspec,
                                                     SysprofSession       *session)
{
  g_assert (SYSPROF_IS_MARK_CHART_ITEM (self));
  g_assert (SYSPROF_IS_SESSION (session));

  g_signal_emit (self, signals[CHANGED], 0);
}

static void
sysprof_mark_chart_item_dispose (GObject *object)
{
  SysprofMarkChartItem *self = (SysprofMarkChartItem *)object;

  g_clear_object (&self->session);
  g_clear_object (&self->catalog);

  G_OBJECT_CLASS (sysprof_mark_chart_item_parent_class)->dispose (object);
}

static void
sysprof_mark_chart_item_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  SysprofMarkChartItem *self = SYSPROF_MARK_CHART_ITEM (object);

  switch (prop_id)
    {
    case PROP_CATALOG:
      g_value_set_object (value, sysprof_mark_chart_item_get_catalog (self));
      break;

    case PROP_SESSION:
      g_value_set_object (value, sysprof_mark_chart_item_get_session (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_mark_chart_item_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  SysprofMarkChartItem *self = SYSPROF_MARK_CHART_ITEM (object);

  switch (prop_id)
    {
    case PROP_CATALOG:
      self->catalog = g_value_dup_object (value);
      break;

    case PROP_SESSION:
      self->session = g_value_dup_object (value);
      g_signal_connect_object (self->session,
                               "notify::selection",
                               G_CALLBACK (sysprof_mark_chart_item_session_notify_selection_cb),
                               self,
                               G_CONNECT_SWAPPED);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_mark_chart_item_class_init (SysprofMarkChartItemClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = sysprof_mark_chart_item_dispose;
  object_class->get_property = sysprof_mark_chart_item_get_property;
  object_class->set_property = sysprof_mark_chart_item_set_property;

  properties[PROP_CATALOG] =
    g_param_spec_object ("catalog", NULL, NULL,
                         SYSPROF_TYPE_MARK_CATALOG,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties[PROP_SESSION] =
    g_param_spec_object ("session", NULL, NULL,
                         SYSPROF_TYPE_SESSION,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  signals[CHANGED] = g_signal_new ("changed",
                                   G_TYPE_FROM_CLASS (klass),
                                   G_SIGNAL_RUN_LAST,
                                   0,
                                   NULL, NULL,
                                   NULL,
                                   G_TYPE_NONE, 0);
}

static void
sysprof_mark_chart_item_init (SysprofMarkChartItem *self)
{
}

SysprofMarkChartItem *
sysprof_mark_chart_item_new (SysprofSession     *session,
                             SysprofMarkCatalog *catalog)
{
  g_return_val_if_fail (SYSPROF_IS_SESSION (session), NULL);
  g_return_val_if_fail (SYSPROF_IS_MARK_CATALOG (catalog), NULL);

  return g_object_new (SYSPROF_TYPE_MARK_CHART_ITEM,
                       "session", session,
                       "catalog", catalog,
                       NULL);
}

SysprofMarkCatalog *
sysprof_mark_chart_item_get_catalog (SysprofMarkChartItem *self)
{
  return self->catalog;
}

SysprofSession *
sysprof_mark_chart_item_get_session (SysprofMarkChartItem *self)
{
  return self->session;
}

typedef struct _LoadTimeSeries
{
  GListModel *model;
  SysprofTimeSpan time_span;
} LoadTimeSeries;

static void
load_time_series_free (LoadTimeSeries *state)
{
  g_clear_object (&state->model);
  g_free (state);
}

static void
load_time_series_worker (GTask        *task,
                         gpointer      source_object,
                         gpointer      task_data,
                         GCancellable *cancellable)
{
  LoadTimeSeries *state = task_data;
  g_autoptr(SysprofTimeSeries) series = NULL;
  guint n_items;

  g_assert (G_IS_TASK (task));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  series = sysprof_time_series_new (state->model, state->time_span);
  n_items = g_list_model_get_n_items (state->model);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(SysprofDocumentMark) mark = g_list_model_get_item (state->model, i);
      SysprofTimeSpan time_span;

      time_span.begin_nsec = sysprof_document_frame_get_time (SYSPROF_DOCUMENT_FRAME (mark));
      time_span.end_nsec = time_span.begin_nsec + sysprof_document_mark_get_duration (mark);

      sysprof_time_series_add (series, time_span, i);
    }

  sysprof_time_series_sort (series);

  g_task_return_pointer (task,
                         g_steal_pointer (&series),
                         (GDestroyNotify)sysprof_time_series_unref);
}

void
sysprof_mark_chart_item_load_time_series (SysprofMarkChartItem *self,
                                          GCancellable         *cancellable,
                                          GAsyncReadyCallback   callback,
                                          gpointer              user_data)
{
  g_autoptr(GTask) task = NULL;
  LoadTimeSeries *state;

  g_return_if_fail (SYSPROF_IS_MARK_CHART_ITEM (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_mark_chart_item_load_time_series);

  if (self->catalog == NULL || self->session == NULL)
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_FAILED,
                               "No available data to generate");
      return;
    }

  state = g_new0 (LoadTimeSeries, 1);
  state->model = g_object_ref (G_LIST_MODEL (self->catalog));
  state->time_span = *sysprof_session_get_selected_time (self->session);

  g_task_set_task_data (task, state, (GDestroyNotify)load_time_series_free);
  g_task_run_in_thread (task, load_time_series_worker);
}

SysprofTimeSeries *
sysprof_mark_chart_item_load_time_series_finish (SysprofMarkChartItem  *self,
                                                 GAsyncResult          *result,
                                                 GError               **error)
{
  g_return_val_if_fail (SYSPROF_IS_MARK_CHART_ITEM (self), NULL);
  g_return_val_if_fail (G_IS_TASK (result), NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}
