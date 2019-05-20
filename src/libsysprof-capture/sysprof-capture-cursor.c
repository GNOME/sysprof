/* sysprof-capture-cursor.c
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

#define G_LOG_DOMAIN "sysprof-capture-cursor"

#include "config.h"

#include "sysprof-capture-condition.h"
#include "sysprof-capture-cursor.h"
#include "sysprof-capture-reader.h"

#define READ_DELEGATE(f) ((ReadDelegate)(f))

typedef const SysprofCaptureFrame *(*ReadDelegate) (SysprofCaptureReader *);

struct _SysprofCaptureCursor
{
  volatile gint         ref_count;
  GPtrArray            *conditions;
  SysprofCaptureReader *reader;
  guint                 reversed : 1;
};

static void
sysprof_capture_cursor_finalize (SysprofCaptureCursor *self)
{
  g_clear_pointer (&self->conditions, g_ptr_array_unref);
  g_clear_pointer (&self->reader, sysprof_capture_reader_unref);
  g_slice_free (SysprofCaptureCursor, self);
}

static SysprofCaptureCursor *
sysprof_capture_cursor_init (void)
{
  SysprofCaptureCursor *self;

  self = g_slice_new0 (SysprofCaptureCursor);
  self->conditions = g_ptr_array_new_with_free_func ((GDestroyNotify) sysprof_capture_condition_unref);
  self->ref_count = 1;

  return g_steal_pointer (&self);
}

/**
 * sysprof_capture_cursor_ref:
 * @self: a #SysprofCaptureCursor
 *
 * Returns: (transfer full): @self
 *
 * Since: 3.34
 */
SysprofCaptureCursor *
sysprof_capture_cursor_ref (SysprofCaptureCursor *self)
{
  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (self->ref_count > 0, NULL);

  g_atomic_int_inc (&self->ref_count);
  return self;
}

/**
 * sysprof_capture_cursor_unref:
 * @self: a #SysprofCaptureCursor
 *
 * Since: 3.34
 */
void
sysprof_capture_cursor_unref (SysprofCaptureCursor *self)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (self->ref_count > 0);

  if (g_atomic_int_dec_and_test (&self->ref_count))
    sysprof_capture_cursor_finalize (self);
}

/**
 * sysprof_capture_cursor_foreach:
 * @self: a #SysprofCaptureCursor
 * @callback: (scope call): a closure to execute
 * @user_data: user data for @callback
 *
 */
void
sysprof_capture_cursor_foreach (SysprofCaptureCursor         *self,
                                SysprofCaptureCursorCallback  callback,
                                gpointer                      user_data)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (self->reader != NULL);
  g_return_if_fail (callback != NULL);

  for (;;)
    {
      const SysprofCaptureFrame *frame;
      SysprofCaptureFrameType type = 0;
      ReadDelegate delegate = NULL;

      if (!sysprof_capture_reader_peek_type (self->reader, &type))
        return;

      switch (type)
        {
        case SYSPROF_CAPTURE_FRAME_TIMESTAMP:
          delegate = READ_DELEGATE (sysprof_capture_reader_read_timestamp);
          break;

        case SYSPROF_CAPTURE_FRAME_SAMPLE:
          delegate = READ_DELEGATE (sysprof_capture_reader_read_sample);
          break;

        case SYSPROF_CAPTURE_FRAME_MAP:
          delegate = READ_DELEGATE (sysprof_capture_reader_read_map);
          break;

        case SYSPROF_CAPTURE_FRAME_MARK:
          delegate = READ_DELEGATE (sysprof_capture_reader_read_mark);
          break;

        case SYSPROF_CAPTURE_FRAME_PROCESS:
          delegate = READ_DELEGATE (sysprof_capture_reader_read_process);
          break;

        case SYSPROF_CAPTURE_FRAME_FORK:
          delegate = READ_DELEGATE (sysprof_capture_reader_read_fork);
          break;

        case SYSPROF_CAPTURE_FRAME_EXIT:
          delegate = READ_DELEGATE (sysprof_capture_reader_read_exit);
          break;

        case SYSPROF_CAPTURE_FRAME_JITMAP:
          delegate = READ_DELEGATE (sysprof_capture_reader_read_jitmap);
          break;

        case SYSPROF_CAPTURE_FRAME_CTRDEF:
          delegate = READ_DELEGATE (sysprof_capture_reader_read_counter_define);
          break;

        case SYSPROF_CAPTURE_FRAME_CTRSET:
          delegate = READ_DELEGATE (sysprof_capture_reader_read_counter_set);
          break;

        case SYSPROF_CAPTURE_FRAME_METADATA:
          delegate = READ_DELEGATE (sysprof_capture_reader_read_metadata);
          break;

        default:
          if (!sysprof_capture_reader_skip (self->reader))
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
              const SysprofCaptureCondition *condition = g_ptr_array_index (self->conditions, i);

              if (sysprof_capture_condition_match (condition, frame))
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
sysprof_capture_cursor_reset (SysprofCaptureCursor *self)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (self->reader != NULL);

  sysprof_capture_reader_reset (self->reader);
}

void
sysprof_capture_cursor_reverse (SysprofCaptureCursor *self)
{
  g_return_if_fail (self != NULL);

  self->reversed = !self->reversed;
}

/**
 * sysprof_capture_cursor_add_condition:
 * @self: An #SysprofCaptureCursor
 * @condition: (transfer full): An #SysprofCaptureCondition
 *
 * Adds the condition to the cursor. This condition must evaluate to
 * true or a given #SysprofCapureFrame will not be matched.
 */
void
sysprof_capture_cursor_add_condition (SysprofCaptureCursor    *self,
                                      SysprofCaptureCondition *condition)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (condition != NULL);

  g_ptr_array_add (self->conditions, condition);
}

/**
 * sysprof_capture_cursor_new:
 * @self: a #SysprofCaptureCursor
 *
 * Returns: (transfer full): a new cursor for @reader
 */
SysprofCaptureCursor *
sysprof_capture_cursor_new (SysprofCaptureReader *reader)
{
  SysprofCaptureCursor *self;

  g_return_val_if_fail (reader != NULL, NULL);

  self = sysprof_capture_cursor_init ();
  self->reader = sysprof_capture_reader_copy (reader);
  sysprof_capture_reader_reset (self->reader);

  return self;
}

/**
 * sysprof_capture_cursor_get_reader:
 *
 * Gets the underlying reader that is used by the cursor.
 *
 * Returns: (transfer none): An #SysprofCaptureReader
 */
SysprofCaptureReader *
sysprof_capture_cursor_get_reader (SysprofCaptureCursor *self)
{
  g_return_val_if_fail (self != NULL, NULL);

  return self->reader;
}
