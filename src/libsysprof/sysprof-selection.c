/* sysprof-selection.c
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

#define G_LOG_DOMAIN "sysprof-selection"

#include "config.h"

#include "sysprof-selection.h"

struct _SysprofSelection
{
  GObject  parent_instance;
  GArray  *ranges;
};

typedef struct
{
  gint64 begin;
  gint64 end;
} Range;

G_DEFINE_TYPE (SysprofSelection, sysprof_selection, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_HAS_SELECTION,
  N_PROPS
};

enum {
  CHANGED,
  N_SIGNALS
};

static GParamSpec *properties [N_PROPS];
static guint signals [N_SIGNALS];

static inline void
int64_swap (gint64 *a,
            gint64 *b)
{
  if (*a > *b)
    {
      gint64 tmp = *a;
      *a = *b;
      *b = tmp;
    }
}

static void
sysprof_selection_finalize (GObject *object)
{
  SysprofSelection *self = (SysprofSelection *)object;

  g_clear_pointer (&self->ranges, g_array_unref);

  G_OBJECT_CLASS (sysprof_selection_parent_class)->finalize (object);
}

static void
sysprof_selection_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  SysprofSelection *self = SYSPROF_SELECTION (object);

  switch (prop_id)
    {
    case PROP_HAS_SELECTION:
      g_value_set_boolean (value, sysprof_selection_get_has_selection (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_selection_class_init (SysprofSelectionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_selection_finalize;
  object_class->get_property = sysprof_selection_get_property;

  properties [PROP_HAS_SELECTION] =
    g_param_spec_boolean ("has-selection",
                          "Has Selection",
                          "Has Selection",
                          FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  /**
   * SysprofSelection::changed:
   *
   * This signal is emitted when the selection has changed.
   */
  signals [CHANGED] =
    g_signal_new ("changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
sysprof_selection_init (SysprofSelection *self)
{
  self->ranges = g_array_new (FALSE, FALSE, sizeof (Range));
}

gboolean
sysprof_selection_get_has_selection (SysprofSelection *self)
{
  g_return_val_if_fail (SYSPROF_IS_SELECTION (self), FALSE);

  return self->ranges->len > 0;
}

/**
 * sysprof_selection_foreach:
 * @self: A #SysprofSelection
 * @foreach_func: (scope call): a callback for each range
 * @user_data: user data for @foreach_func
 *
 * Calls @foreach_func for every selected range.
 */
void
sysprof_selection_foreach (SysprofSelection            *self,
                           SysprofSelectionForeachFunc  foreach_func,
                           gpointer                     user_data)
{
  g_return_if_fail (SYSPROF_IS_SELECTION (self));
  g_return_if_fail (foreach_func != NULL);

  for (guint i = 0; i < self->ranges->len; i++)
    {
      const Range *range = &g_array_index (self->ranges, Range, i);
      foreach_func (self, range->begin, range->end, user_data);
    }
}

static gint
range_compare (gconstpointer a,
               gconstpointer b)
{
  const Range *ra = a;
  const Range *rb = b;

  if (ra->begin < rb->begin)
    return -1;

  if (rb->begin < ra->begin)
    return 1;

  if (ra->end < rb->end)
    return -1;

  if (rb->end < ra->end)
    return 1;

  return 0;
}

static void
join_overlapping (GArray *ranges)
{
  if (ranges->len > 1)
    {
      guint i = 0;

      while (i < ranges->len - 1)
        {
          Range *range;
          Range next;

          range = &g_array_index (ranges, Range, i);
          next = g_array_index (ranges, Range, i + 1);

          if (range->end > next.begin)
            {
              range->end = next.end;
              g_array_remove_index (ranges, i + 1);
            }
          else
            {
              i++;
            }
        }
    }
}

void
sysprof_selection_select_range (SysprofSelection *self,
                                gint64            begin_time,
                                gint64            end_time)
{
  Range range = { 0 };

  g_return_if_fail (SYSPROF_IS_SELECTION (self));

  int64_swap (&begin_time, &end_time);

  range.begin = begin_time;
  range.end = end_time;

  g_array_append_val (self->ranges, range);
  g_array_sort (self->ranges, range_compare);
  join_overlapping (self->ranges);

  if (self->ranges->len == 1)
    g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_HAS_SELECTION]);

  g_signal_emit (self, signals [CHANGED], 0);
}

void
sysprof_selection_unselect_range (SysprofSelection *self,
                                  gint64            begin,
                                  gint64            end)
{
  g_return_if_fail (SYSPROF_IS_SELECTION (self));

  int64_swap (&begin, &end);

  for (guint i = 0; i < self->ranges->len; i++)
    {
      const Range *range = &g_array_index (self->ranges, Range, i);

      if (range->begin == begin && range->end == end)
        {
          g_array_remove_index (self->ranges, i);
          if (self->ranges->len == 0)
            g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_HAS_SELECTION]);
          g_signal_emit (self, signals [CHANGED], 0);
          break;
        }
    }
}

void
sysprof_selection_unselect_all (SysprofSelection *self)
{
  g_return_if_fail (SYSPROF_IS_SELECTION (self));

  if (self->ranges->len > 0)
    {
      g_array_remove_range (self->ranges, 0, self->ranges->len);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_HAS_SELECTION]);
      g_signal_emit (self, signals [CHANGED], 0);
    }
}

gboolean
sysprof_selection_contains (SysprofSelection *self,
                            gint64            time_at)
{
  if (self == NULL || self->ranges->len == 0)
    return TRUE;

  for (guint i = 0; i < self->ranges->len; i++)
    {
      const Range *range = &g_array_index (self->ranges, Range, i);

      if (time_at >= range->begin && time_at <= range->end)
        return TRUE;
    }

  return FALSE;
}

SysprofSelection *
sysprof_selection_copy (const SysprofSelection *self)
{
  SysprofSelection *copy;

  if (self == NULL)
    return NULL;

  copy = g_object_new (SYSPROF_TYPE_SELECTION, NULL);

  for (guint i = 0; i < self->ranges->len; i++)
    {
      Range range = g_array_index (self->ranges, Range, i);
      g_array_append_val (copy->ranges, range);
    }

  return copy;
}

guint
sysprof_selection_get_n_ranges (SysprofSelection *self)
{
  g_return_val_if_fail (SYSPROF_IS_SELECTION (self), 0);
  return self->ranges ? self->ranges->len : 0;
}

void
sysprof_selection_get_nth_range (SysprofSelection *self,
                                 guint             nth,
                                 gint64           *begin_time,
                                 gint64           *end_time)
{
  Range r = {0};

  g_return_if_fail (SYSPROF_IS_SELECTION (self));

  if (self->ranges && nth < self->ranges->len)
    r = g_array_index (self->ranges, Range, nth);

  if (begin_time)
    *begin_time = r.begin;

  if (end_time)
    *end_time = r.end;
}
