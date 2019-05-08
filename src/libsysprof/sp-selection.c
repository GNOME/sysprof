/* sp-selection.c
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

#define G_LOG_DOMAIN "sp-selection"

#include "config.h"

#include "sp-selection.h"

struct _SpSelection
{
  GObject  parent_instance;
  GArray  *ranges;
};

typedef struct
{
  gint64 begin;
  gint64 end;
} Range;

G_DEFINE_TYPE (SpSelection, sp_selection, G_TYPE_OBJECT)

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
sp_selection_finalize (GObject *object)
{
  SpSelection *self = (SpSelection *)object;

  g_clear_pointer (&self->ranges, g_array_unref);

  G_OBJECT_CLASS (sp_selection_parent_class)->finalize (object);
}

static void
sp_selection_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  SpSelection *self = SP_SELECTION (object);

  switch (prop_id)
    {
    case PROP_HAS_SELECTION:
      g_value_set_boolean (value, sp_selection_get_has_selection (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_selection_class_init (SpSelectionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sp_selection_finalize;
  object_class->get_property = sp_selection_get_property;

  properties [PROP_HAS_SELECTION] =
    g_param_spec_boolean ("has-selection",
                          "Has Selection",
                          "Has Selection",
                          FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  /**
   * SpSelection::changed:
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
sp_selection_init (SpSelection *self)
{
  self->ranges = g_array_new (FALSE, FALSE, sizeof (Range));
}

gboolean
sp_selection_get_has_selection (SpSelection *self)
{
  g_return_val_if_fail (SP_IS_SELECTION (self), FALSE);

  return self->ranges->len > 0;
}

/**
 * sp_selection_foreach:
 * @self: A #SpSelection
 * @foreach_func: (scope call): a callback for each range
 * @user_data: user data for @foreach_func
 *
 * Calls @foreach_func for every selected range.
 */
void
sp_selection_foreach (SpSelection            *self,
                      SpSelectionForeachFunc  foreach_func,
                      gpointer                user_data)
{
  g_return_if_fail (SP_IS_SELECTION (self));
  g_return_if_fail (foreach_func != NULL);

  for (guint i = 0; i < self->ranges->len; i++)
    {
      const Range *range = &g_array_index (self->ranges, Range, i);
      foreach_func (self, range->begin, range->end, user_data);
    }
}

void
sp_selection_select_range (SpSelection *self,
                           gint64       begin_time,
                           gint64       end_time)
{
  Range range = { 0 };

  g_return_if_fail (SP_IS_SELECTION (self));

  int64_swap (&begin_time, &end_time);

  range.begin = begin_time;
  range.end = end_time;

  g_array_append_val (self->ranges, range);

  if (self->ranges->len == 1)
    g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_HAS_SELECTION]);
  g_signal_emit (self, signals [CHANGED], 0);
}

void
sp_selection_unselect_range (SpSelection *self,
                             gint64       begin,
                             gint64       end)
{
  g_return_if_fail (SP_IS_SELECTION (self));

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
sp_selection_unselect_all (SpSelection *self)
{
  g_return_if_fail (SP_IS_SELECTION (self));

  if (self->ranges->len > 0)
    {
      g_array_remove_range (self->ranges, 0, self->ranges->len);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_HAS_SELECTION]);
      g_signal_emit (self, signals [CHANGED], 0);
    }
}

gboolean
sp_selection_contains (SpSelection *self,
                       gint64       time_at)
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

SpSelection *
sp_selection_copy (const SpSelection *self)
{
  SpSelection *copy;

  if (self == NULL)
    return NULL;

  copy = g_object_new (SP_TYPE_SELECTION, NULL);

  for (guint i = 0; i < self->ranges->len; i++)
    {
      Range range = g_array_index (self->ranges, Range, i);
      g_array_append_val (copy->ranges, range);
    }

  return copy;
}
