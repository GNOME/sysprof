/* sp-capture-cursor.c
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

#define G_LOG_DOMAIN "sp-capture-cursor"

#include "config.h"

#include "sp-capture-condition.h"
#include "sp-capture-cursor.h"
#include "sp-capture-reader.h"

#define READ_DELEGATE(f) ((ReadDelegate)(f))

typedef const SpCaptureFrame *(*ReadDelegate) (SpCaptureReader *);

struct _SpCaptureCursor
{
  volatile gint    ref_count;
  GPtrArray       *conditions;
  SpCaptureReader *reader;
  guint            reversed : 1;
};

static void
sp_capture_cursor_finalize (SpCaptureCursor *self)
{
  g_clear_pointer (&self->conditions, g_ptr_array_unref);
  g_clear_pointer (&self->reader, sp_capture_reader_unref);
  g_slice_free (SpCaptureCursor, self);
}

static SpCaptureCursor *
sp_capture_cursor_init (void)
{
  SpCaptureCursor *self;

  self = g_slice_new0 (SpCaptureCursor);
  self->conditions = g_ptr_array_new_with_free_func ((GDestroyNotify) sp_capture_condition_unref);
  self->ref_count = 1;

  return g_steal_pointer (&self);
}

/**
 * sp_capture_cursor_ref:
 * @self: a #SpCaptureCursor
 *
 * Returns: (transfer full): @self
 *
 * Since: 3.34
 */
SpCaptureCursor *
sp_capture_cursor_ref (SpCaptureCursor *self)
{
  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (self->ref_count > 0, NULL);

  g_atomic_int_inc (&self->ref_count);
  return self;
}

/**
 * sp_capture_cursor_unref:
 * @self: a #SpCaptureCursor
 *
 * Since: 3.34
 */
void
sp_capture_cursor_unref (SpCaptureCursor *self)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (self->ref_count > 0);

  if (g_atomic_int_dec_and_test (&self->ref_count))
    sp_capture_cursor_finalize (self);
}

/**
 * sp_capture_cursor_foreach:
 * @self: a #SpCaptureCursor
 * @callback: (scope call): a closure to execute
 * @user_data: user data for @callback
 *
 */
void
sp_capture_cursor_foreach (SpCaptureCursor         *self,
                           SpCaptureCursorCallback  callback,
                           gpointer                 user_data)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (self->reader != NULL);
  g_return_if_fail (callback != NULL);

  for (;;)
    {
      const SpCaptureFrame *frame;
      SpCaptureFrameType type = 0;
      ReadDelegate delegate = NULL;

      if (!sp_capture_reader_peek_type (self->reader, &type))
        return;

      switch (type)
        {
        case SP_CAPTURE_FRAME_TIMESTAMP:
          delegate = READ_DELEGATE (sp_capture_reader_read_timestamp);
          break;

        case SP_CAPTURE_FRAME_SAMPLE:
          delegate = READ_DELEGATE (sp_capture_reader_read_sample);
          break;

        case SP_CAPTURE_FRAME_MAP:
          delegate = READ_DELEGATE (sp_capture_reader_read_map);
          break;

        case SP_CAPTURE_FRAME_MARK:
          delegate = READ_DELEGATE (sp_capture_reader_read_mark);
          break;

        case SP_CAPTURE_FRAME_PROCESS:
          delegate = READ_DELEGATE (sp_capture_reader_read_process);
          break;

        case SP_CAPTURE_FRAME_FORK:
          delegate = READ_DELEGATE (sp_capture_reader_read_fork);
          break;

        case SP_CAPTURE_FRAME_EXIT:
          delegate = READ_DELEGATE (sp_capture_reader_read_exit);
          break;

        case SP_CAPTURE_FRAME_JITMAP:
          delegate = READ_DELEGATE (sp_capture_reader_read_jitmap);
          break;

        case SP_CAPTURE_FRAME_CTRDEF:
          delegate = READ_DELEGATE (sp_capture_reader_read_counter_define);
          break;

        case SP_CAPTURE_FRAME_CTRSET:
          delegate = READ_DELEGATE (sp_capture_reader_read_counter_set);
          break;

        default:
          if (!sp_capture_reader_skip (self->reader))
            return;
          delegate = NULL;
          break;
        }

      if (delegate == NULL)
        continue;

      if (NULL == (frame = delegate (self->reader)))
        return;

      if (self->conditions->len == 0)
        {
          if (!callback (frame, user_data))
            return;
        }
      else
        {
          for (guint i = 0; i < self->conditions->len; i++)
            {
              const SpCaptureCondition *condition = g_ptr_array_index (self->conditions, i);

              if (sp_capture_condition_match (condition, frame))
                {
                  if (!callback (frame, user_data))
                    return;
                  break;
                }
            }
        }
    }
}

void
sp_capture_cursor_reset (SpCaptureCursor *self)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (self->reader != NULL);

  sp_capture_reader_reset (self->reader);
}

void
sp_capture_cursor_reverse (SpCaptureCursor *self)
{
  g_return_if_fail (self != NULL);

  self->reversed = !self->reversed;
}

/**
 * sp_capture_cursor_add_condition:
 * @self: An #SpCaptureCursor
 * @condition: (transfer full): An #SpCaptureCondition
 *
 * Adds the condition to the cursor. This condition must evaluate to
 * true or a given #SpCapureFrame will not be matched.
 */
void
sp_capture_cursor_add_condition (SpCaptureCursor    *self,
                                 SpCaptureCondition *condition)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (condition != NULL);

  g_ptr_array_add (self->conditions, condition);
}

/**
 * sp_capture_cursor_new:
 * @self: a #SpCaptureCursor
 *
 * Returns: (transfer full): a new cursor for @reader
 */
SpCaptureCursor *
sp_capture_cursor_new (SpCaptureReader *reader)
{
  SpCaptureCursor *self;

  g_return_val_if_fail (reader != NULL, NULL);

  self = sp_capture_cursor_init ();
  self->reader = sp_capture_reader_copy (reader);
  sp_capture_reader_reset (self->reader);

  return self;
}

/**
 * sp_capture_cursor_get_reader:
 *
 * Gets the underlying reader that is used by the cursor.
 *
 * Returns: (transfer none): An #SpCaptureReader
 */
SpCaptureReader *
sp_capture_cursor_get_reader (SpCaptureCursor *self)
{
  g_return_val_if_fail (self != NULL, NULL);

  return self->reader;
}
