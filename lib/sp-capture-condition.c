/* sp-capture-condition.c
 *
 * Copyright (C) 2016 Christian Hergert <chergert@redhat.com>
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
 */

#define G_LOG_DOMAIN "sp-capture-condition"

#include <string.h>

#include "sp-capture-condition.h"

typedef enum
{
  SP_CAPTURE_CONDITION_WHERE_TYPE_IN,
  SP_CAPTURE_CONDITION_WHERE_TIME_BETWEEN,
  SP_CAPTURE_CONDITION_WHERE_PID_IN,
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
          if (frame->pid == g_array_index (self->u.where_pid_in, GPid, i))
            return TRUE;
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
    case SP_CAPTURE_CONDITION_WHERE_TYPE_IN:
      return sp_capture_condition_new_where_type_in (
          self->u.where_type_in->len, (const SpCaptureFrameType *)self->u.where_type_in->data);

    case SP_CAPTURE_CONDITION_WHERE_TIME_BETWEEN:
      break;

    case SP_CAPTURE_CONDITION_WHERE_PID_IN:
      return sp_capture_condition_new_where_pid_in (
          self->u.where_pid_in->len, (const GPid *)self->u.where_pid_in->data);

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
    case SP_CAPTURE_CONDITION_WHERE_TYPE_IN:
      g_array_free (self->u.where_type_in, TRUE);
      break;

    case SP_CAPTURE_CONDITION_WHERE_TIME_BETWEEN:
      break;

    case SP_CAPTURE_CONDITION_WHERE_PID_IN:
      g_array_free (self->u.where_pid_in, TRUE);
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
sp_capture_condition_new_where_pid_in (guint       n_pids,
                                       const GPid *pids)
{
  SpCaptureCondition *self;

  g_return_val_if_fail (pids != NULL, NULL);

  self = g_slice_new0 (SpCaptureCondition);
  self->type = SP_CAPTURE_CONDITION_WHERE_PID_IN;
  self->u.where_pid_in = g_array_sized_new (FALSE, FALSE, sizeof (GPid), n_pids);
  g_array_set_size (self->u.where_pid_in, n_pids);
  memcpy (self->u.where_pid_in->data, pids, sizeof (GPid) * n_pids);

  return self;
}
