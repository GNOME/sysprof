/* sysprof-frame-cycle.c
 *
 * Copyright 2026 Christian Hergert
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

#include <libdex.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "sysprof-frame-cycle-private.h"
#include "sysprof-document-private.h"
#include "sysprof-document-storage-private.h"
#include "sysprof-document-frame.h"
#include "sysprof-document-mark.h"

typedef struct
{
  char                  *name;
  int                    pid;
  SysprofFrameCycleKind  kind;
  guint                  rows;
  GQueue                 pending;
  GArray                *row_ends;
} FrameLane;

struct _SysprofFrameCycle
{
  gatomicrefcount  ref_count;
  GPtrArray       *frames;
  GPtrArray       *lanes;
  gboolean         has_compositor;
};

G_DEFINE_BOXED_TYPE (SysprofFrameCycle, sysprof_frame_cycle, sysprof_frame_cycle_ref, sysprof_frame_cycle_unref)

static void
frame_lane_free (gpointer data)
{
  FrameLane *lane = data;

  g_free (lane->name);
  g_queue_clear (&lane->pending);
  g_clear_pointer (&lane->row_ends, g_array_unref);
  g_free (lane);
}

static void
frame_cycle_frame_free (gpointer data)
{
  SysprofFrameCycleFrame *frame = data;

  g_clear_object (&frame->dispatch_mark);
  g_clear_object (&frame->presentation_mark);
  g_free (frame);
}

static guint
frame_cycle_find_lane (SysprofFrameCycle     *self,
                       SysprofFrameCycleKind  kind,
                       int                    pid,
                       const char            *name)
{
  FrameLane *lane;

  for (guint i = 0; i < self->lanes->len; i++)
    {
      lane = g_ptr_array_index (self->lanes, i);

      if (lane->kind == kind &&
          lane->pid == pid &&
          g_strcmp0 (lane->name, name) == 0)
        return i;
    }

  lane = g_new0 (FrameLane, 1);
  lane->kind = kind;
  lane->pid = pid;
  lane->name = g_strdup (name);
  lane->row_ends = g_array_new (FALSE, FALSE, sizeof (gint64));

  g_ptr_array_add (self->lanes, lane);

  return self->lanes->len - 1;
}

static gint64
parse_presentation (const char *message,
                    gint64      callback_time)
{
  const char *p;
  char *end;
  gint64 offset;

  if (message == NULL)
    return 0;

  if ((p = strstr (message, "presentation was ")) != NULL)
    {
      p += strlen ("presentation was ");
      offset = g_ascii_strtoll (p, &end, 10);

      if (end == p || !g_str_has_prefix (end, " µs earlier") || offset < 0 ||
          callback_time < 0 || offset > callback_time / 1000)
        return 0;

      return callback_time - offset * 1000;
    }

  if ((p = strstr (message, "presentation will be ")) != NULL)
    {
      p += strlen ("presentation will be ");
      offset = g_ascii_strtoll (p, &end, 10);

      if (end == p || !g_str_has_prefix (end, " µs later") || offset < 0 ||
          offset > (G_MAXINT64 - callback_time) / 1000)
        return 0;

      return callback_time + offset * 1000;
    }

  return 0;
}

static gint64
parse_kms_ready (const char *message)
{
  const char *p;
  char *end;
  gint64 value;

  if (message == NULL || (p = strstr (message, "KMS update ready at ")) == NULL)
    return 0;

  p += strlen ("KMS update ready at ");
  value = g_ascii_strtoll (p, &end, 10);

  if (end == p || !g_str_has_prefix (end, " µs") || value <= 0 || value > G_MAXINT64 / 1000)
    return 0;

  return value * 1000;
}

static char *
compositor_output (const char *message)
{
  const char *end;

  if (message == NULL || message[0] == 0)
    return NULL;

  end = strchr (message, ',');

  if (end == NULL)
    return g_strdup (message);

  return g_strndup (message, end - message);
}

static gboolean
parse_field (const char *message,
             const char *field,
             guint64    *value)
{
  g_autofree char *needle = NULL;
  const char *p;
  char *end;
  guint64 parsed;

  if (message == NULL)
    return FALSE;

  needle = g_strdup_printf ("%s=", field);
  p = message;

  while ((p = strstr (p, needle)) != NULL)
    {
      if (p == message || p[-1] == ' ' || p[-1] == ',')
        break;

      p++;
    }

  if (p == NULL)
    return FALSE;

  p += strlen (needle);

  if (*p == '-')
    return FALSE;

  errno = 0;
  parsed = g_ascii_strtoull (p, &end, g_str_has_prefix (p, "0x") ? 16 : 10);

  if (errno == ERANGE || end == p || (*end != 0 && *end != ' ' && *end != ','))
    return FALSE;

  *value = parsed;

  return TRUE;
}

static gint64
parse_microseconds (const char *message,
                    const char *field)
{
  guint64 value;

  if (!parse_field (message, field, &value) || value > G_MAXINT64 / 1000)
    return 0;

  return value * 1000;
}

static char *
frame_key (int     pid,
           guint64 clock,
           guint64 frame)
{
  return g_strdup_printf ("%d:%" G_GUINT64_FORMAT ":%" G_GUINT64_FORMAT,
                          pid, clock, frame);
}

static int
mark_compare (gconstpointer a,
              gconstpointer b)
{
  SysprofDocumentMark * const *ma = a;
  SysprofDocumentMark * const *mb = b;
  gint64 ta = sysprof_document_frame_get_time (SYSPROF_DOCUMENT_FRAME (*ma));
  gint64 tb = sysprof_document_frame_get_time (SYSPROF_DOCUMENT_FRAME (*mb));

  return (ta > tb) - (ta < tb);
}

static int
frame_compare (gconstpointer a,
               gconstpointer b)
{
  SysprofFrameCycleFrame * const *fa = a;
  SysprofFrameCycleFrame * const *fb = b;

  return ((*fa)->begin > (*fb)->begin) - ((*fa)->begin < (*fb)->begin);
}

/**
 * sysprof_frame_cycle_new:
 * @document: a recording document
 *
 * Builds a corpus of compositor and application frame work from known marks.
 * Legacy compositor completions are paired with the oldest pending dispatch
 * for the same process and output. GTK and compositor frames are associated
 * only when their recorded presentation timestamps have a unique match.
 *
 * Returns: (transfer full): a frame corpus
 */
SysprofFrameCycle *
sysprof_frame_cycle_new (SysprofDocument *document)
{
  g_autoptr(SysprofDocumentStorage) storage = NULL;
  g_autoptr(SysprofDocumentReader) reader = NULL;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (document), NULL);

  storage = _sysprof_document_dup_storage (document);
  reader = _sysprof_document_reader_new (storage, NULL);

  return _sysprof_document_reader_create_frames (reader);
}

SysprofFrameCycle *
_sysprof_frame_cycle_new_for_reader (SysprofDocumentReader *reader)
{
  static const char * const mark_names[] = {
    "Clutter::FrameClock::dispatch()",
    "Clutter::FrameClock::presented()",
    "Clutter::FrameClock::ready()",
    "Frameclock cycle",
    "Frameclock update",
    "Frameclock layout",
    "Frameclock paint",
    "Presented window",
    NULL,
  };
  g_autoptr(GListModel) marks = NULL;
  g_autoptr(GPtrArray) candidates = NULL;
  g_autoptr(GHashTable) application_frames = NULL;
  g_autoptr(GHashTable) application_ids = NULL;
  g_autoptr(GHashTable) compositor_ids = NULL;
  g_autoptr(GHashTable) presentations = NULL;
  SysprofFrameCycle *self;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_READER (reader), NULL);

  self = g_new0 (SysprofFrameCycle, 1);
  g_atomic_ref_count_init (&self->ref_count);
  self->frames = g_ptr_array_new_with_free_func (frame_cycle_frame_free);
  self->lanes = g_ptr_array_new_with_free_func (frame_lane_free);

  candidates = g_ptr_array_new_with_free_func (g_object_unref);
  application_ids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  compositor_ids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  marks = _sysprof_document_reader_list_named_marks (reader, mark_names);

  for (guint i = 0; i < g_list_model_get_n_items (marks); i++)
    {
      g_autoptr(SysprofDocumentMark) mark = g_list_model_get_item (marks, i);

      if (_sysprof_document_reader_is_cancelled (reader))
        goto cancelled;

      g_ptr_array_add (candidates, g_steal_pointer (&mark));
    }

  if (_sysprof_document_reader_is_cancelled (reader))
    goto cancelled;

  g_ptr_array_sort (candidates, mark_compare);

  if (_sysprof_document_reader_is_cancelled (reader))
    goto cancelled;

  for (guint i = 0; i < candidates->len; i++)
    {
      SysprofDocumentMark *mark = g_ptr_array_index (candidates, i);
      SysprofDocumentFrame *source = SYSPROF_DOCUMENT_FRAME (mark);
      const char *name = sysprof_document_mark_get_name (mark);
      const char *message = sysprof_document_mark_get_message (mark);
      gint64 time = sysprof_document_frame_get_time (source);
      gint64 duration = MAX (0, sysprof_document_mark_get_duration (mark));
      int pid = sysprof_document_frame_get_pid (source);
      guint lane_index;
      guint64 clock;
      guint64 number;
      SysprofFrameCycleFrame *frame = NULL;

      if (_sysprof_document_reader_is_cancelled (reader))
        goto cancelled;

      if (g_str_equal (name, "Frameclock cycle"))
        {
          g_autofree char *lane_name = g_strdup_printf ("GTK · PID %d", pid);

          lane_index = frame_cycle_find_lane (self,
                                              SYSPROF_FRAME_CYCLE_APPLICATION,
                                              pid,
                                              lane_name);

          frame = g_new0 (SysprofFrameCycleFrame, 1);
          frame->begin = time;
          frame->dispatch_end = time + duration;
          frame->end = frame->dispatch_end;
          frame->lane = lane_index;
          frame->pid = pid;
          frame->kind = SYSPROF_FRAME_CYCLE_APPLICATION;
          frame->result = SYSPROF_FRAME_CYCLE_WORK_COMPLETE;
          frame->dispatch_mark = g_object_ref (mark);
          frame->matching_compositor = -1;

          if (parse_field (message, "clock", &clock) &&
              parse_field (message, "frame", &number) &&
              number <= G_MAXINT64)
            {
              frame->gtk_clock = clock;
              frame->gtk_frame_number = number;

              g_hash_table_replace (application_ids, frame_key (pid, clock, number), frame);
            }

          g_ptr_array_add (self->frames, frame);

          continue;
        }

      if (g_str_equal (name, "Presented window"))
        {
          if (parse_field (message, "clock", &clock) &&
              parse_field (message, "frame", &number))
            {
              g_autofree char *key = frame_key (pid, clock, number);

              if ((frame = g_hash_table_lookup (application_ids, key)))
                {
                  frame->presentation = time;
                  frame->end = MAX (frame->end, time);
                  frame->result = SYSPROF_FRAME_CYCLE_PRESENTED;
                  g_set_object (&frame->presentation_mark, mark);
                }
            }

          continue;
        }

      if (g_str_equal (name, "Frameclock update") ||
          g_str_equal (name, "Frameclock layout") ||
          g_str_equal (name, "Frameclock paint"))
        continue;

      if (message == NULL || message[0] == 0)
        continue;

      {
        g_autofree char *output = compositor_output (message);

        lane_index = frame_cycle_find_lane (self, SYSPROF_FRAME_CYCLE_COMPOSITOR, pid, output);
      }

      if (g_str_equal (name, "Clutter::FrameClock::dispatch()"))
        {
          frame = g_new0 (SysprofFrameCycleFrame, 1);
          frame->begin = time;
          frame->dispatch_end = time + duration;
          frame->end = frame->dispatch_end;
          frame->lane = lane_index;
          frame->pid = pid;
          frame->kind = SYSPROF_FRAME_CYCLE_COMPOSITOR;
          frame->dispatch_mark = g_object_ref (mark);
          frame->matching_compositor = -1;

          if (parse_field (message, "clock", &clock) &&
              parse_field (message, "frame", &number) &&
              clock <= G_MAXUINT &&
              number <= G_MAXINT64)
            {
              frame->clock_id = clock;
              frame->frame_number = number;
              frame->target_presentation = parse_microseconds (message, "target_us");
              frame->submission_deadline = parse_microseconds (message, "submit_deadline_us");

              g_hash_table_replace (compositor_ids, frame_key (pid, clock, number), frame);
            }

          g_ptr_array_add (self->frames, frame);
          g_queue_push_tail (&((FrameLane *)g_ptr_array_index (self->lanes, lane_index))->pending,
                             frame);

          continue;
        }

      if (parse_field (message, "clock", &clock) &&
          parse_field (message, "frame", &number))
        {
          g_autofree char *key = frame_key (pid, clock, number);

          frame = g_hash_table_lookup (compositor_ids, key);

          if (frame != NULL)
            g_queue_remove (&((FrameLane *)g_ptr_array_index (self->lanes, frame->lane))->pending,
                            frame);
        }
      else
        {
          FrameLane *lane = g_ptr_array_index (self->lanes, lane_index);

          if (g_str_equal (name, "Clutter::FrameClock::ready()") &&
              g_queue_get_length (&lane->pending) > 1)
            frame = g_queue_pop_tail (&lane->pending);
          else
            frame = g_queue_pop_head (&lane->pending);

          if (frame != NULL)
            frame->correlation_estimated = TRUE;
        }

      if (frame == NULL)
        continue;

      frame->end = MAX (frame->dispatch_end, time + duration);

      if (g_str_equal (name, "Clutter::FrameClock::ready()"))
        {
          frame->result = SYSPROF_FRAME_CYCLE_READY;
        }
      else
        {
          frame->result = SYSPROF_FRAME_CYCLE_PRESENTED;
          g_set_object (&frame->presentation_mark, mark);
          frame->presentation = parse_microseconds (message, "actual_us");

          if (frame->presentation == 0)
            {
              frame->presentation = parse_presentation (message, time);
              frame->presentation_estimated = frame->presentation != 0;
            }

          frame->kms_ready = parse_kms_ready (message);
          frame->end = MAX (frame->end, frame->presentation);
        }
    }

  if (_sysprof_document_reader_is_cancelled (reader))
    goto cancelled;

  g_ptr_array_sort (self->frames, frame_compare);

  if (_sysprof_document_reader_is_cancelled (reader))
    goto cancelled;

  application_frames = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                               NULL, (GDestroyNotify)g_ptr_array_unref);

  for (guint i = 0; i < self->frames->len; i++)
    {
      SysprofFrameCycleFrame *frame = g_ptr_array_index (self->frames, i);
      GPtrArray *by_pid;

      if (_sysprof_document_reader_is_cancelled (reader))
        goto cancelled;

      if (frame->kind != SYSPROF_FRAME_CYCLE_APPLICATION)
        continue;

      by_pid = g_hash_table_lookup (application_frames, GINT_TO_POINTER (frame->pid));

      if (by_pid == NULL)
        {
          by_pid = g_ptr_array_new ();
          g_hash_table_insert (application_frames, GINT_TO_POINTER (frame->pid), by_pid);
        }

      g_ptr_array_add (by_pid, frame);
    }

  for (guint i = 0; i < candidates->len; i++)
    {
      SysprofDocumentMark *mark = g_ptr_array_index (candidates, i);
      const char *name = sysprof_document_mark_get_name (mark);
      gint64 time;
      gint64 end;
      int pid;
      GPtrArray *by_pid;
      const char *message;
      guint64 clock;
      guint64 number;
      guint low;
      guint high;

      if (_sysprof_document_reader_is_cancelled (reader))
        goto cancelled;

      if (!g_str_equal (name, "Frameclock update") &&
          !g_str_equal (name, "Frameclock layout") &&
          !g_str_equal (name, "Frameclock paint"))
        continue;

      time = sysprof_document_frame_get_time (SYSPROF_DOCUMENT_FRAME (mark));
      end = time + MAX (0, sysprof_document_mark_get_duration (mark));
      pid = sysprof_document_frame_get_pid (SYSPROF_DOCUMENT_FRAME (mark));
      message = sysprof_document_mark_get_message (mark);

      if (parse_field (message, "clock", &clock) &&
          parse_field (message, "frame", &number))
        {
          g_autofree char *key = frame_key (pid, clock, number);
          SysprofFrameCycleFrame *frame = g_hash_table_lookup (application_ids, key);

          if (frame != NULL)
            {
              if (g_str_equal (name, "Frameclock update"))
                frame->update_begin = time, frame->update_end = end;
              else if (g_str_equal (name, "Frameclock layout"))
                frame->layout_begin = time, frame->layout_end = end;
              else
                frame->paint_begin = time, frame->paint_end = end;
            }

          continue;
        }

      by_pid = g_hash_table_lookup (application_frames, GINT_TO_POINTER (pid));

      if (by_pid == NULL)
        continue;

      low = 0;
      high = by_pid->len;

      while (low < high)
        {
          guint middle = low + (high - low) / 2;
          SysprofFrameCycleFrame *candidate = g_ptr_array_index (by_pid, middle);

          if (candidate->begin <= time)
            low = middle + 1;
          else
            high = middle;
        }

      for (guint j = low; j > 0; j--)
        {
          SysprofFrameCycleFrame *frame = g_ptr_array_index (by_pid, j - 1);

          if (_sysprof_document_reader_is_cancelled (reader))
            goto cancelled;

          if (end > frame->end)
            continue;

          if (g_str_equal (name, "Frameclock update"))
            frame->update_begin = time, frame->update_end = end;
          else if (g_str_equal (name, "Frameclock layout"))
            frame->layout_begin = time, frame->layout_end = end;
          else
            frame->paint_begin = time, frame->paint_end = end;

          break;
        }
    }

  presentations = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, NULL);

  for (guint i = 0; i < self->frames->len; i++)
    {
      SysprofFrameCycleFrame *frame = g_ptr_array_index (self->frames, i);
      gint64 *timestamp;

      if (_sysprof_document_reader_is_cancelled (reader))
        goto cancelled;

      if (frame->kind != SYSPROF_FRAME_CYCLE_COMPOSITOR ||
          frame->presentation == 0 || frame->presentation_estimated)
        continue;

      timestamp = g_new (gint64, 1);
      *timestamp = frame->presentation;

      if (g_hash_table_contains (presentations, timestamp))
        g_hash_table_replace (presentations, timestamp, NULL);
      else
        g_hash_table_insert (presentations, timestamp, GUINT_TO_POINTER (i + 1));
    }

  for (guint i = 0; i < self->frames->len; i++)
    {
      SysprofFrameCycleFrame *frame = g_ptr_array_index (self->frames, i);
      gpointer match;

      if (_sysprof_document_reader_is_cancelled (reader))
        goto cancelled;

      if (frame->kind != SYSPROF_FRAME_CYCLE_APPLICATION || frame->presentation == 0)
        continue;

      match = g_hash_table_lookup (presentations, &frame->presentation);

      if (match != NULL)
        frame->matching_compositor = GPOINTER_TO_UINT (match) - 1;
    }

  for (guint i = 0; i < self->frames->len; i++)
    {
      SysprofFrameCycleFrame *frame = g_ptr_array_index (self->frames, i);
      FrameLane *lane = g_ptr_array_index (self->lanes, frame->lane);
      guint row = 0;

      if (_sysprof_document_reader_is_cancelled (reader))
        goto cancelled;

      for (; row < lane->row_ends->len; row++)
        {
          if (_sysprof_document_reader_is_cancelled (reader))
            goto cancelled;

          if (g_array_index (lane->row_ends, gint64, row) <= frame->begin)
            break;
        }

      if (row == lane->row_ends->len)
        g_array_append_val (lane->row_ends, frame->end);
      else
        g_array_index (lane->row_ends, gint64, row) = frame->end;

      self->has_compositor |= frame->kind == SYSPROF_FRAME_CYCLE_COMPOSITOR;
      frame->row = row;
      lane->rows = MAX (lane->rows, row + 1);
    }

  return self;

cancelled:
  sysprof_frame_cycle_unref (self);

  return NULL;
}

/**
 * sysprof_frame_cycle_ref:
 * @self: a frame corpus
 *
 * Returns: (transfer full): a reference to @self
 */
SysprofFrameCycle *
sysprof_frame_cycle_ref (SysprofFrameCycle *self)
{
  g_return_val_if_fail (self != NULL, NULL);

  g_atomic_ref_count_inc (&self->ref_count);

  return self;
}

static void
sysprof_frame_cycle_unref_destroy (gpointer data)
{
  SysprofFrameCycle *self = data;

  g_ptr_array_unref (self->frames);
  g_ptr_array_unref (self->lanes);
  g_free (self);
}

/**
 * sysprof_frame_cycle_unref:
 * @self: a frame corpus
 *
 * Releases a reference to @self.
 */
void
sysprof_frame_cycle_unref (SysprofFrameCycle *self)
{
  g_return_if_fail (self != NULL);

  if (g_atomic_ref_count_dec (&self->ref_count))
    {
      if (dex_scheduler_get_default () != NULL)
        dex_scheduler_push (dex_thread_pool_scheduler_get_default (),
                            sysprof_frame_cycle_unref_destroy,
                            self);
      else
        sysprof_frame_cycle_unref_destroy (self);
    }
}

/**
 * sysprof_frame_cycle_get_n_frames:
 * @self: a frame corpus
 *
 * Returns: the number of recorded frame cycles
 */
guint
sysprof_frame_cycle_get_n_frames (SysprofFrameCycle *self)
{
  g_return_val_if_fail (self != NULL, 0);

  return self->frames->len;
}

/**
 * sysprof_frame_cycle_get_frame:
 * @self: a frame corpus
 * @position: the frame position
 *
 * Returns: (transfer none) (nullable): frame timing data
 */
const SysprofFrameCycleFrame *
sysprof_frame_cycle_get_frame (SysprofFrameCycle *self,
                               guint              position)
{
  g_return_val_if_fail (self != NULL, NULL);

  return position < self->frames->len ? g_ptr_array_index (self->frames, position) : NULL;
}

/**
 * sysprof_frame_cycle_get_n_lanes:
 * @self: a frame corpus
 *
 * Returns: the number of output and application lanes
 */
guint
sysprof_frame_cycle_get_n_lanes (SysprofFrameCycle *self)
{
  g_return_val_if_fail (self != NULL, 0);

  return self->lanes->len;
}

/**
 * sysprof_frame_cycle_get_lane:
 * @self: a frame corpus
 * @position: the lane position
 *
 * Returns: (transfer none) (nullable): the lane title
 */
const char *
sysprof_frame_cycle_get_lane (SysprofFrameCycle *self,
                              guint              position)
{
  FrameLane *lane;

  g_return_val_if_fail (self != NULL, NULL);

  if (position >= self->lanes->len)
    return NULL;

  lane = g_ptr_array_index (self->lanes, position);

  return lane->name;
}

/**
 * sysprof_frame_cycle_get_lane_rows:
 * @self: a frame corpus
 * @position: the lane position
 *
 * Returns: the number of rows needed to show overlapping frames
 */
guint
sysprof_frame_cycle_get_lane_rows (SysprofFrameCycle *self,
                                   guint              position)
{
  FrameLane *lane;

  g_return_val_if_fail (self != NULL, 0);

  if (position >= self->lanes->len)
    return 0;

  lane = g_ptr_array_index (self->lanes, position);

  return lane->rows;
}

gboolean
_sysprof_frame_cycle_has_compositor (SysprofFrameCycle *self)
{
  g_return_val_if_fail (self != NULL, FALSE);

  return self->has_compositor;
}
