/* sp-capture-condition.c
 *
 * Copyright © 2016 Christian Hergert <chergert@redhat.com>
 *
 * This file is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This file is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#define G_LOG_DOMAIN "sp-capture-condition"

#include <string.h>

#include "capture/sp-capture-condition.h"

/**
 * SECTION:sp-capture-condition
 * @title: SpCaptureCondition
 *
 * The #SpCaptureCondition type is an abstraction on an operation
 * for a sort of AST to the #SpCaptureCursor. The goal is that if
 * we abstract the types of fields we want to match in the cursor
 * that we can opportunistically add indexes to speed up the operation
 * later on without changing the implementation of cursor consumers.
 */

typedef enum
{
  SP_CAPTURE_CONDITION_AND,
  SP_CAPTURE_CONDITION_WHERE_TYPE_IN,
  SP_CAPTURE_CONDITION_WHERE_TIME_BETWEEN,
  SP_CAPTURE_CONDITION_WHERE_PID_IN,
  SP_CAPTURE_CONDITION_WHERE_COUNTER_IN,
} SpCaptureConditionType;

struct _SpCaptureCondition
{
  SpCaptureConditionType type;
  union {
    GArray *where_type_in;
    struct {
      gint64 begin;
      gint64 end;
    } where_time_between;
    GArray *where_pid_in;
    GArray *where_counter_in;
    struct {
      SpCaptureCondition *left;
      SpCaptureCondition *right;
    } and;
  } u;
};

gboolean
sp_capture_condition_match (const SpCaptureCondition *self,
                            const SpCaptureFrame     *frame)
{
  g_assert (self != NULL);
  g_assert (frame != NULL);

  switch (self->type)
    {
    case SP_CAPTURE_CONDITION_AND:
      return sp_capture_condition_match (self->u.and.left, frame) &&
             sp_capture_condition_match (self->u.and.right, frame);

    case SP_CAPTURE_CONDITION_WHERE_TYPE_IN:
      for (guint i = 0; i < self->u.where_type_in->len; i++)
        {
          if (frame->type == g_array_index (self->u.where_type_in, SpCaptureFrameType, i))
            return TRUE;
        }
      return FALSE;

    case SP_CAPTURE_CONDITION_WHERE_TIME_BETWEEN:
      return (frame->time >= self->u.where_time_between.begin && frame->time <= self->u.where_time_between.end);

    case SP_CAPTURE_CONDITION_WHERE_PID_IN:
      for (guint i = 0; i < self->u.where_pid_in->len; i++)
        {
          if (frame->pid == g_array_index (self->u.where_pid_in, gint32, i))
            return TRUE;
        }
      return FALSE;

    case SP_CAPTURE_CONDITION_WHERE_COUNTER_IN:
      if (frame->type == SP_CAPTURE_FRAME_CTRSET)
        {
          const SpCaptureFrameCounterSet *set = (SpCaptureFrameCounterSet *)frame;

          for (guint i = 0; i < self->u.where_counter_in->len; i++)
            {
              guint counter = g_array_index (self->u.where_counter_in, guint, i);

              for (guint j = 0; j < set->n_values; j++)
                {
                  if (counter == set->values[j].ids[0] ||
                      counter == set->values[j].ids[1] ||
                      counter == set->values[j].ids[2] ||
                      counter == set->values[j].ids[3] ||
                      counter == set->values[j].ids[4] ||
                      counter == set->values[j].ids[5] ||
                      counter == set->values[j].ids[6] ||
                      counter == set->values[j].ids[7])
                    return TRUE;
                }
            }
        }
      else if (frame->type == SP_CAPTURE_FRAME_CTRDEF)
        {
          const SpCaptureFrameCounterDefine *def = (SpCaptureFrameCounterDefine *)frame;

          for (guint i = 0; i < self->u.where_counter_in->len; i++)
            {
              guint counter = g_array_index (self->u.where_counter_in, guint, i);

              for (guint j = 0; j < def->n_counters; j++)
                {
                  if (def->counters[j].id == counter)
                    return TRUE;
                }
            }
        }

      return FALSE;

    default:
      break;
    }

  g_assert_not_reached ();

  return FALSE;
}

static SpCaptureCondition *
sp_capture_condition_copy (const SpCaptureCondition *self)
{
  SpCaptureCondition *copy;

  copy = g_slice_new0 (SpCaptureCondition);
  copy->type = self->type;

  switch (self->type)
    {
    case SP_CAPTURE_CONDITION_AND:
      return sp_capture_condition_new_and (
        sp_capture_condition_copy (self->u.and.left),
        sp_capture_condition_copy (self->u.and.right));

    case SP_CAPTURE_CONDITION_WHERE_TYPE_IN:
      return sp_capture_condition_new_where_type_in (
          self->u.where_type_in->len,
          (const SpCaptureFrameType *)(gpointer)self->u.where_type_in->data);

    case SP_CAPTURE_CONDITION_WHERE_TIME_BETWEEN:
      break;

    case SP_CAPTURE_CONDITION_WHERE_PID_IN:
      return sp_capture_condition_new_where_pid_in (
          self->u.where_pid_in->len,
          (const gint32 *)(gpointer)self->u.where_pid_in->data);

    case SP_CAPTURE_CONDITION_WHERE_COUNTER_IN:
      return sp_capture_condition_new_where_counter_in (
          self->u.where_counter_in->len,
          (const guint *)(gpointer)self->u.where_counter_in->data);

    default:
      g_assert_not_reached ();
      break;
    }

  return copy;
}

static void
sp_capture_condition_free (SpCaptureCondition *self)
{
  switch (self->type)
    {
    case SP_CAPTURE_CONDITION_AND:
      sp_capture_condition_free (self->u.and.left);
      sp_capture_condition_free (self->u.and.right);
      break;

    case SP_CAPTURE_CONDITION_WHERE_TYPE_IN:
      g_array_free (self->u.where_type_in, TRUE);
      break;

    case SP_CAPTURE_CONDITION_WHERE_TIME_BETWEEN:
      break;

    case SP_CAPTURE_CONDITION_WHERE_PID_IN:
      g_array_free (self->u.where_pid_in, TRUE);
      break;

    case SP_CAPTURE_CONDITION_WHERE_COUNTER_IN:
      g_array_free (self->u.where_counter_in, TRUE);
      break;

    default:
      g_assert_not_reached ();
      break;
    }

  g_slice_free (SpCaptureCondition, self);
}

G_DEFINE_BOXED_TYPE (SpCaptureCondition,
                     sp_capture_condition,
                     sp_capture_condition_copy,
                     sp_capture_condition_free)

SpCaptureCondition *
sp_capture_condition_new_where_type_in (guint                     n_types,
                                        const SpCaptureFrameType *types)
{
  SpCaptureCondition *self;

  g_return_val_if_fail (types != NULL, NULL);

  self = g_slice_new0 (SpCaptureCondition);
  self->type = SP_CAPTURE_CONDITION_WHERE_TYPE_IN;
  self->u.where_type_in = g_array_sized_new (FALSE, FALSE, sizeof (SpCaptureFrameType), n_types);
  g_array_set_size (self->u.where_type_in, n_types);
  memcpy (self->u.where_type_in->data, types, sizeof (SpCaptureFrameType) * n_types);

  return self;
}

SpCaptureCondition *
sp_capture_condition_new_where_time_between (gint64 begin_time,
                                             gint64 end_time)
{
  SpCaptureCondition *self;

  if G_UNLIKELY (begin_time > end_time)
    {
      gint64 tmp = begin_time;
      begin_time = end_time;
      end_time = tmp;
    }

  self = g_slice_new0 (SpCaptureCondition);
  self->type = SP_CAPTURE_CONDITION_WHERE_TIME_BETWEEN;
  self->u.where_time_between.begin = begin_time;
  self->u.where_time_between.end = end_time;

  return self;
}

SpCaptureCondition *
sp_capture_condition_new_where_pid_in (guint         n_pids,
                                       const gint32 *pids)
{
  SpCaptureCondition *self;

  g_return_val_if_fail (pids != NULL, NULL);

  self = g_slice_new0 (SpCaptureCondition);
  self->type = SP_CAPTURE_CONDITION_WHERE_PID_IN;
  self->u.where_pid_in = g_array_sized_new (FALSE, FALSE, sizeof (gint32), n_pids);
  g_array_set_size (self->u.where_pid_in, n_pids);
  memcpy (self->u.where_pid_in->data, pids, sizeof (gint32) * n_pids);

  return self;
}

SpCaptureCondition *
sp_capture_condition_new_where_counter_in (guint        n_counters,
                                           const guint *counters)
{
  SpCaptureCondition *self;

  g_return_val_if_fail (counters != NULL || n_counters == 0, NULL);

  self = g_slice_new0 (SpCaptureCondition);
  self->type = SP_CAPTURE_CONDITION_WHERE_COUNTER_IN;
  self->u.where_counter_in = g_array_sized_new (FALSE, FALSE, sizeof (guint), n_counters);

  if (n_counters > 0)
    {
      g_array_set_size (self->u.where_counter_in, n_counters);
      memcpy (self->u.where_counter_in->data, counters, sizeof (guint) * n_counters);
    }

  return self;
}

/**
 * sp_capture_condition_new_and:
 * @left: (transfer full): An #SpCaptureCondition
 * @right: (transfer full): An #SpCaptureCondition
 *
 * Creates a new #SpCaptureCondition that requires both left and right
 * to evaluate to %TRUE.
 *
 * Returns: (transfer full): A new #SpCaptureCondition.
 */
SpCaptureCondition *
sp_capture_condition_new_and (SpCaptureCondition *left,
                              SpCaptureCondition *right)
{
  SpCaptureCondition *self;

  g_return_val_if_fail (left != NULL, NULL);
  g_return_val_if_fail (right != NULL, NULL);

  self = g_slice_new0 (SpCaptureCondition);
  self->type = SP_CAPTURE_CONDITION_AND;
  self->u.and.left = left;
  self->u.and.right = right;

  return self;
}
