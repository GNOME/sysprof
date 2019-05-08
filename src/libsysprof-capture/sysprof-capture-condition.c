/* sysprof-capture-condition.c
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
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
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "sysprof-capture-condition"

#include "config.h"

#include <string.h>

#include "sysprof-capture-condition.h"

/**
 * SECTION:sysprof-capture-condition
 * @title: SysprofCaptureCondition
 *
 * The #SysprofCaptureCondition type is an abstraction on an operation
 * for a sort of AST to the #SysprofCaptureCursor. The goal is that if
 * we abstract the types of fields we want to match in the cursor
 * that we can opportunistically add indexes to speed up the operation
 * later on without changing the implementation of cursor consumers.
 */

typedef enum
{
  SYSPROF_CAPTURE_CONDITION_AND,
  SYSPROF_CAPTURE_CONDITION_WHERE_TYPE_IN,
  SYSPROF_CAPTURE_CONDITION_WHERE_TIME_BETWEEN,
  SYSPROF_CAPTURE_CONDITION_WHERE_PID_IN,
  SYSPROF_CAPTURE_CONDITION_WHERE_COUNTER_IN,
} SysprofCaptureConditionType;

struct _SysprofCaptureCondition
{
  volatile gint ref_count;
  SysprofCaptureConditionType type;
  union {
    GArray *where_type_in;
    struct {
      gint64 begin;
      gint64 end;
    } where_time_between;
    GArray *where_pid_in;
    GArray *where_counter_in;
    struct {
      SysprofCaptureCondition *left;
      SysprofCaptureCondition *right;
    } and;
  } u;
};

gboolean
sysprof_capture_condition_match (const SysprofCaptureCondition *self,
                            const SysprofCaptureFrame     *frame)
{
  g_assert (self != NULL);
  g_assert (frame != NULL);

  switch (self->type)
    {
    case SYSPROF_CAPTURE_CONDITION_AND:
      return sysprof_capture_condition_match (self->u.and.left, frame) &&
             sysprof_capture_condition_match (self->u.and.right, frame);

    case SYSPROF_CAPTURE_CONDITION_WHERE_TYPE_IN:
      for (guint i = 0; i < self->u.where_type_in->len; i++)
        {
          if (frame->type == g_array_index (self->u.where_type_in, SysprofCaptureFrameType, i))
            return TRUE;
        }
      return FALSE;

    case SYSPROF_CAPTURE_CONDITION_WHERE_TIME_BETWEEN:
      return (frame->time >= self->u.where_time_between.begin && frame->time <= self->u.where_time_between.end);

    case SYSPROF_CAPTURE_CONDITION_WHERE_PID_IN:
      for (guint i = 0; i < self->u.where_pid_in->len; i++)
        {
          if (frame->pid == g_array_index (self->u.where_pid_in, gint32, i))
            return TRUE;
        }
      return FALSE;

    case SYSPROF_CAPTURE_CONDITION_WHERE_COUNTER_IN:
      if (frame->type == SYSPROF_CAPTURE_FRAME_CTRSET)
        {
          const SysprofCaptureFrameCounterSet *set = (SysprofCaptureFrameCounterSet *)frame;

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
      else if (frame->type == SYSPROF_CAPTURE_FRAME_CTRDEF)
        {
          const SysprofCaptureFrameCounterDefine *def = (SysprofCaptureFrameCounterDefine *)frame;

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

static SysprofCaptureCondition *
sysprof_capture_condition_init (void)
{
  SysprofCaptureCondition *self;

  self = g_slice_new0 (SysprofCaptureCondition);
  self->ref_count = 1;

  return g_steal_pointer (&self);
}

SysprofCaptureCondition *
sysprof_capture_condition_copy (const SysprofCaptureCondition *self)
{
  SysprofCaptureCondition *copy;

  copy = sysprof_capture_condition_init ();
  copy->type = self->type;

  switch (self->type)
    {
    case SYSPROF_CAPTURE_CONDITION_AND:
      return sysprof_capture_condition_new_and (
        sysprof_capture_condition_copy (self->u.and.left),
        sysprof_capture_condition_copy (self->u.and.right));

    case SYSPROF_CAPTURE_CONDITION_WHERE_TYPE_IN:
      return sysprof_capture_condition_new_where_type_in (
          self->u.where_type_in->len,
          (const SysprofCaptureFrameType *)(gpointer)self->u.where_type_in->data);

    case SYSPROF_CAPTURE_CONDITION_WHERE_TIME_BETWEEN:
      break;

    case SYSPROF_CAPTURE_CONDITION_WHERE_PID_IN:
      return sysprof_capture_condition_new_where_pid_in (
          self->u.where_pid_in->len,
          (const gint32 *)(gpointer)self->u.where_pid_in->data);

    case SYSPROF_CAPTURE_CONDITION_WHERE_COUNTER_IN:
      return sysprof_capture_condition_new_where_counter_in (
          self->u.where_counter_in->len,
          (const guint *)(gpointer)self->u.where_counter_in->data);

    default:
      g_assert_not_reached ();
      break;
    }

  return copy;
}

static void
sysprof_capture_condition_finalize (SysprofCaptureCondition *self)
{
  switch (self->type)
    {
    case SYSPROF_CAPTURE_CONDITION_AND:
      sysprof_capture_condition_unref (self->u.and.left);
      sysprof_capture_condition_unref (self->u.and.right);
      break;

    case SYSPROF_CAPTURE_CONDITION_WHERE_TYPE_IN:
      g_array_free (self->u.where_type_in, TRUE);
      break;

    case SYSPROF_CAPTURE_CONDITION_WHERE_TIME_BETWEEN:
      break;

    case SYSPROF_CAPTURE_CONDITION_WHERE_PID_IN:
      g_array_free (self->u.where_pid_in, TRUE);
      break;

    case SYSPROF_CAPTURE_CONDITION_WHERE_COUNTER_IN:
      g_array_free (self->u.where_counter_in, TRUE);
      break;

    default:
      g_assert_not_reached ();
      break;
    }

  g_slice_free (SysprofCaptureCondition, self);
}

SysprofCaptureCondition *
sysprof_capture_condition_ref (SysprofCaptureCondition *self)
{
  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (self->ref_count > 0, NULL);

  g_atomic_int_inc (&self->ref_count);
  return self;
}

void
sysprof_capture_condition_unref (SysprofCaptureCondition *self)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (self->ref_count > 0);

  if (g_atomic_int_dec_and_test (&self->ref_count))
    sysprof_capture_condition_finalize (self);
}

SysprofCaptureCondition *
sysprof_capture_condition_new_where_type_in (guint                     n_types,
                                        const SysprofCaptureFrameType *types)
{
  SysprofCaptureCondition *self;

  g_return_val_if_fail (types != NULL, NULL);

  self = sysprof_capture_condition_init ();
  self->type = SYSPROF_CAPTURE_CONDITION_WHERE_TYPE_IN;
  self->u.where_type_in = g_array_sized_new (FALSE, FALSE, sizeof (SysprofCaptureFrameType), n_types);
  g_array_set_size (self->u.where_type_in, n_types);
  memcpy (self->u.where_type_in->data, types, sizeof (SysprofCaptureFrameType) * n_types);

  return self;
}

SysprofCaptureCondition *
sysprof_capture_condition_new_where_time_between (gint64 begin_time,
                                             gint64 end_time)
{
  SysprofCaptureCondition *self;

  if G_UNLIKELY (begin_time > end_time)
    {
      gint64 tmp = begin_time;
      begin_time = end_time;
      end_time = tmp;
    }

  self = sysprof_capture_condition_init ();
  self->type = SYSPROF_CAPTURE_CONDITION_WHERE_TIME_BETWEEN;
  self->u.where_time_between.begin = begin_time;
  self->u.where_time_between.end = end_time;

  return self;
}

SysprofCaptureCondition *
sysprof_capture_condition_new_where_pid_in (guint         n_pids,
                                       const gint32 *pids)
{
  SysprofCaptureCondition *self;

  g_return_val_if_fail (pids != NULL, NULL);

  self = sysprof_capture_condition_init ();
  self->type = SYSPROF_CAPTURE_CONDITION_WHERE_PID_IN;
  self->u.where_pid_in = g_array_sized_new (FALSE, FALSE, sizeof (gint32), n_pids);
  g_array_set_size (self->u.where_pid_in, n_pids);
  memcpy (self->u.where_pid_in->data, pids, sizeof (gint32) * n_pids);

  return self;
}

SysprofCaptureCondition *
sysprof_capture_condition_new_where_counter_in (guint        n_counters,
                                           const guint *counters)
{
  SysprofCaptureCondition *self;

  g_return_val_if_fail (counters != NULL || n_counters == 0, NULL);

  self = sysprof_capture_condition_init ();
  self->type = SYSPROF_CAPTURE_CONDITION_WHERE_COUNTER_IN;
  self->u.where_counter_in = g_array_sized_new (FALSE, FALSE, sizeof (guint), n_counters);

  if (n_counters > 0)
    {
      g_array_set_size (self->u.where_counter_in, n_counters);
      memcpy (self->u.where_counter_in->data, counters, sizeof (guint) * n_counters);
    }

  return self;
}

/**
 * sysprof_capture_condition_new_and:
 * @left: (transfer full): An #SysprofCaptureCondition
 * @right: (transfer full): An #SysprofCaptureCondition
 *
 * Creates a new #SysprofCaptureCondition that requires both left and right
 * to evaluate to %TRUE.
 *
 * Returns: (transfer full): A new #SysprofCaptureCondition.
 */
SysprofCaptureCondition *
sysprof_capture_condition_new_and (SysprofCaptureCondition *left,
                              SysprofCaptureCondition *right)
{
  SysprofCaptureCondition *self;

  g_return_val_if_fail (left != NULL, NULL);
  g_return_val_if_fail (right != NULL, NULL);

  self = sysprof_capture_condition_init ();
  self->type = SYSPROF_CAPTURE_CONDITION_AND;
  self->u.and.left = left;
  self->u.and.right = right;

  return self;
}
