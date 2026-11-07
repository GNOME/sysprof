/* capture-summary.c
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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libdex.h>
#include <json-glib/json-glib.h>
#include <sysprof.h>

#include "sysprof-symbol-private.h"

typedef struct
{
  char                  *key;
  int                    pid;
  guint                  lane;
  guint64                clock;
  SysprofFrameCycleKind  kind;
  guint                  results[4];
  guint                  observed;
  guint                  estimated;
  guint                  correlation_estimated;
  guint                  inconsistent;
  guint                  presentation_before_work_end;
  guint                  target_eligible;
  guint                  target_late;
  guint                  work_over_budget;
  guint                  duplicate_presentations;
  GArray                *metrics[9];
  GArray                *presentations;
} FrameStream;

typedef struct
{
  int     pid;
  char   *group;
  char   *name;
  guint   points;
  GArray *durations;
} MarkGroup;

typedef struct
{
  guint pid;
  guint tid;
  guint count;
  guint cpu_kind[4];
} ThreadSummary;

typedef struct
{
  int                    pid;
  char                  *name;
  char                  *binary;
  guint                  count;
  gint64                 time;
  guint                  depth;
  guint                  raw_depth;
  gboolean               fallback_addresses[8];
  SysprofDocumentSample *representative;
  guint64                addresses[8];
  guint                  positions[8];
  char                  *symbols[8];
  SysprofSourceLocation *locations[8];
} LeafSummary;

static gboolean opt_no_symbols;

typedef struct
{
  SysprofSourceLocation *location;
  gboolean               complete;
} SourceLocationRequest;

static void
source_location_cb (GObject      *object,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  SourceLocationRequest *request = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (request != NULL);

  request->location = sysprof_document_resolve_source_location_finish (SYSPROF_DOCUMENT (object),
                                                                       result,
                                                                       &error);
  request->complete = TRUE;
}

static SysprofSourceLocation *
resolve_source_location (SysprofDocument          *document,
                         SysprofDocumentTraceable *traceable,
                         guint                     position)
{
  g_autoptr(GMainContext) context = g_main_context_ref_thread_default ();
  SourceLocationRequest request = { 0 };

  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (SYSPROF_IS_DOCUMENT_TRACEABLE (traceable));

  sysprof_document_resolve_source_location_async (document,
                                                  traceable,
                                                  position,
                                                  NULL,
                                                  source_location_cb,
                                                  &request);

  while (!request.complete)
    g_main_context_iteration (context, TRUE);

  return request.location;
}

static SysprofSymbol *
resolve_physical_address (SysprofDocument          *document,
                          SysprofDocumentTraceable *traceable,
                          guint                     position)
{
  SysprofSymbol **before;
  SysprofSymbol **after;
  SysprofSymbol *result = NULL;
  guint n_before = 0;
  guint n_after;

  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (SYSPROF_IS_DOCUMENT_TRACEABLE (traceable));

  before = g_new0 (SysprofSymbol *, position + 1);
  after = g_new0 (SysprofSymbol *, position + 1);

  if (position > 0)
    n_before = sysprof_document_symbolize_traceable (document,
                                                     traceable,
                                                     before,
                                                     position,
                                                     NULL);

  n_after = sysprof_document_symbolize_traceable (document,
                                                  traceable,
                                                  after,
                                                  position + 1,
                                                  NULL);

  if (n_after > n_before)
    result = after[n_after - 1];
  else if (n_before > 0 && n_before < position + 1 &&
           after[n_before] == after[n_before - 1])
    result = after[n_before];

  g_free (before);
  g_free (after);

  return result;
}

static void
leaf_summary_free (LeafSummary *leaf)
{
  g_free (leaf->name);
  g_free (leaf->binary);

  for (guint i = 0; i < G_N_ELEMENTS (leaf->symbols); i++)
    {
      g_free (leaf->symbols[i]);
      g_clear_object (&leaf->locations[i]);
    }

  g_clear_object (&leaf->representative);
  g_free (leaf);
}

static int
compare_leaves (gconstpointer a,
                gconstpointer b)
{
  const LeafSummary *x = *(LeafSummary *const *)a;
  const LeafSummary *y = *(LeafSummary *const *)b;
  int ret = (y->count > x->count) - (y->count < x->count);

  if (ret != 0)
    return ret;

  if (x->pid != y->pid)
    return (x->pid > y->pid) - (x->pid < y->pid);

  return g_strcmp0 (x->name, y->name);
}

static void
resolve_stack (SysprofDocument *document,
               LeafSummary     *leaf)
{
  SysprofDocumentTraceable *traceable;

  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (leaf != NULL);

  if (opt_no_symbols)
    return;

  traceable = SYSPROF_DOCUMENT_TRACEABLE (leaf->representative);

  for (guint i = 0; i < leaf->depth; i++)
    {
      SysprofSymbol *current = resolve_physical_address (document, traceable,
                                                         leaf->positions[i]);

      g_clear_pointer (&leaf->symbols[i], g_free);

      if (current != NULL)
        {
          leaf->symbols[i] = g_strdup (sysprof_symbol_get_name (current));
          leaf->fallback_addresses[i] = current->is_fallback;
        }

      leaf->locations[i] = resolve_source_location (document, traceable,
                                                    leaf->positions[i]);
    }
}

static const char *metric_names[] = {
  "work",
  "cycle_to_present",
  "post_work_wait",
  "update",
  "layout",
  "paint",
  "target_delta",
  "presentation_interval",
  "kms_ready_deadline_delta"
};

static gboolean opt_json;
static int opt_process = -1;
static int opt_frame = -1;
static int opt_top = 8;
static int opt_worst = 5;
static double opt_from = -1;
static double opt_to = -1;
static double opt_budget = -1;
static char *opt_counter;
static gint64 origin;
static gint64 selection_begin;
static gint64 selection_end;
static JsonBuilder *builder;

static void
add_int (const char *name,
         gint64      value)
{
  json_builder_set_member_name (builder, name);
  json_builder_add_int_value (builder, value);
}

static void
add_double (const char *name,
            double      value)
{
  json_builder_set_member_name (builder, name);

  if (isfinite (value))
    json_builder_add_double_value (builder, value);
  else
    json_builder_add_null_value (builder);
}

static void
add_bool (const char *name,
          gboolean    value)
{
  json_builder_set_member_name (builder, name);
  json_builder_add_boolean_value (builder, value);
}

static void
add_string (const char *name,
            const char *value)
{
  g_autofree char *valid = NULL;
  g_autofree char *shortened = NULL;
  g_autofree char *bounded = NULL;
  gsize length;

  json_builder_set_member_name (builder, name);

  if (value == NULL)
    {
      json_builder_add_null_value (builder);

      return;
    }

  valid = g_utf8_make_valid (value, -1);
  length = strlen (valid);

  if (length > 512)
    {
      char *end = valid + 500;

      while (end > valid && ((*end & 0xc0) == 0x80))
        end--;

      shortened = g_strndup (valid, end - valid);
      bounded = g_strconcat (shortened, "[truncated]", NULL);
      json_builder_add_string_value (builder, bounded);
    }
  else
    {
      json_builder_add_string_value (builder, valid);
    }
}

static void
add_source_location (SysprofSourceLocation *location)
{
  g_autoptr(GFile) file = NULL;
  g_autofree char *path = NULL;
  g_autofree char *display = NULL;
  guint line = 0;
  guint character = 0;

  if (location != NULL)
    {
      file = sysprof_source_location_dup_file (location);
      path = g_file_get_path (file);

      if (path == NULL)
        path = g_file_get_uri (file);

      line = sysprof_source_location_get_line (location);
      character = sysprof_source_location_get_character (location);

      if (line > 0 && character > 0)
        display = g_strdup_printf ("%s:%u:%u", path, line, character);
      else if (line > 0)
        display = g_strdup_printf ("%s:%u", path, line);
      else
        display = g_strdup (path);
    }

  add_string ("source_location", display);
  add_string ("source_file", path);
  json_builder_set_member_name (builder, "source_line");

  if (line > 0)
    json_builder_add_int_value (builder, line);
  else
    json_builder_add_null_value (builder);

  json_builder_set_member_name (builder, "source_character");

  if (character > 0)
    json_builder_add_int_value (builder, character);
  else
    json_builder_add_null_value (builder);
}

static void

begin_object (const char *name)
{
  if (name != NULL)
    json_builder_set_member_name (builder, name);

  json_builder_begin_object (builder);
}

static void

begin_array (const char *name)
{
  json_builder_set_member_name (builder, name);
  json_builder_begin_array (builder);
}

static double
ms (gint64 nanoseconds)
{
  return nanoseconds / 1000000.0;
}

static gboolean
in_window (gint64 time)
{
  return time >= selection_begin && time < selection_end;
}

static int
compare_int64 (gconstpointer a,
               gconstpointer b)
{
  gint64 x = *(const gint64 *)a;
  gint64 y = *(const gint64 *)b;

  return (x > y) - (x < y);
}

static void
append_duration (GArray *array,
                 gint64  begin,
                 gint64  end)
{
  gint64 duration;

  if (begin <= 0 || end < begin)
    return;

  duration = end - begin;
  g_array_append_val (array, duration);
}

static void
write_distribution (const char *name,
                    GArray     *array)
{
  long double total = 0;
  gint64 *values;
  guint n;

  begin_object (name);
  n = array->len;
  add_int ("n", n);

  if (n == 0)
    {
      const char *keys[] = { "min_ms", "p50_ms", "p95_ms", "p99_ms", "max_ms", "mean_ms", "total_ms" };

      for (guint i = 0; i < G_N_ELEMENTS (keys); i++)
        add_double (keys[i], NAN);
    }
  else
    {
      g_array_sort (array, compare_int64);
      values = (gint64 *)array->data;

      for (guint i = 0; i < n; i++)
        total += values[i];

      add_double ("min_ms", ms (values[0]));
      add_double ("p50_ms", ms (values[(n * 50 + 99) / 100 - 1]));
      add_double ("p95_ms", ms (values[(n * 95 + 99) / 100 - 1]));
      add_double ("p99_ms", ms (values[(n * 99 + 99) / 100 - 1]));
      add_double ("max_ms", ms (values[n - 1]));
      add_double ("mean_ms", (double)(total / n / 1000000.0));
      add_double ("total_ms", (double)(total / 1000000.0));
    }

  json_builder_end_object (builder);
}

static void
frame_stream_free (FrameStream *stream)
{
  g_free (stream->key);

  for (guint i = 0; i < G_N_ELEMENTS (stream->metrics); i++)
    g_array_unref (stream->metrics[i]);

  g_array_unref (stream->presentations);
  g_free (stream);
}

static FrameStream *
find_stream (GPtrArray                    *streams,
             SysprofFrameCycle            *cycle,
             const SysprofFrameCycleFrame *frame)
{
  const char *lane = sysprof_frame_cycle_get_lane (cycle, frame->lane);
  g_autofree char *key = g_strdup_printf ("%d/%d/%u/%s/%" G_GUINT64_FORMAT,
                                          frame->kind,
                                          frame->pid,
                                          frame->lane,
                                          lane ? lane : "",
                                          frame->gtk_clock ? frame->gtk_clock : frame->clock_id);

  FrameStream *stream;

  for (guint i = 0; i < streams->len; i++)
    {
      stream = g_ptr_array_index (streams, i);

      if (g_str_equal (stream->key, key))
        return stream;
    }

  stream = g_new0 (FrameStream, 1);
  stream->key = g_steal_pointer (&key);
  stream->pid = frame->pid;
  stream->lane = frame->lane;
  stream->kind = frame->kind;
  stream->clock = frame->gtk_clock ? frame->gtk_clock : frame->clock_id;

  for (guint i = 0; i < G_N_ELEMENTS (stream->metrics); i++)
    stream->metrics[i] = g_array_new (FALSE, FALSE, sizeof (gint64));

  stream->presentations = g_array_new (FALSE, FALSE, sizeof (gint64));

  g_ptr_array_add (streams, stream);

  return stream;
}

static gboolean
exact_presentation (const SysprofFrameCycleFrame *frame)
{
  return frame->presentation > 0 && frame->dispatch_end >= frame->begin &&
         !frame->presentation_estimated && !frame->correlation_estimated;
}

static void
accumulate_frame (FrameStream                  *stream,
                  const SysprofFrameCycleFrame *frame)
{
  gint64 value;

  stream->results[frame->result]++;
  stream->estimated += frame->presentation_estimated;
  stream->correlation_estimated += frame->correlation_estimated;

  if (frame->presentation > 0 && !frame->presentation_estimated)
    stream->observed++;

  if (frame->dispatch_end > 0 && frame->dispatch_end < frame->begin)
    stream->inconsistent++;

  append_duration (stream->metrics[0], frame->begin, frame->dispatch_end);
  append_duration (stream->metrics[3], frame->update_begin, frame->update_end);
  append_duration (stream->metrics[4], frame->layout_begin, frame->layout_end);
  append_duration (stream->metrics[5], frame->paint_begin, frame->paint_end);

  if (opt_budget > 0 && frame->dispatch_end - frame->begin > opt_budget * 1000000)
    stream->work_over_budget++;

  if (!exact_presentation (frame))
    return;

  append_duration (stream->metrics[1], frame->begin, frame->presentation);

  if (frame->presentation >= frame->dispatch_end)
    append_duration (stream->metrics[2], frame->dispatch_end, frame->presentation);
  else
    stream->presentation_before_work_end++;

  if (stream->clock != 0)
    {
      value = frame->presentation;
      g_array_append_val (stream->presentations, value);
    }

  if (frame->kind == SYSPROF_FRAME_CYCLE_COMPOSITOR && frame->target_presentation > 0)
    {
      value = frame->presentation - frame->target_presentation;
      g_array_append_val (stream->metrics[6], value);
      stream->target_eligible++;
      stream->target_late += value > 0;
    }

  if (frame->kind == SYSPROF_FRAME_CYCLE_COMPOSITOR &&
      frame->kms_ready > 0 &&
      frame->submission_deadline > 0)
    {
      value = frame->kms_ready - frame->submission_deadline;
      g_array_append_val (stream->metrics[8], value);
    }
}

static void
write_frame_example (SysprofFrameCycle *cycle,
                     guint              index,
                     const char        *reason)
{
  const SysprofFrameCycleFrame *frame = sysprof_frame_cycle_get_frame (cycle, index);
  const SysprofFrameCycleFrame *match = NULL;
  const char *results[] = { "incomplete", "work_complete", "ready", "presented" };
  g_autofree char *clock = NULL;
  g_autofree char *match_clock = NULL;

  begin_object (NULL);
  add_int ("index", index);
  add_string ("selected_for", reason);
  add_string ("kind", frame->kind == SYSPROF_FRAME_CYCLE_APPLICATION ? "application" : "compositor");
  add_int ("pid", frame->pid);
  add_string ("lane", sysprof_frame_cycle_get_lane (cycle, frame->lane));
  add_int ("lane_index", frame->lane);
  clock = g_strdup_printf ("0x%" G_GINT64_MODIFIER "x",
                           frame->gtk_clock
                             ? frame->gtk_clock
                             : (guint64)frame->clock_id);
  add_string ("clock", clock);
  add_string ("result", results[frame->result]);

  if (frame->kind == SYSPROF_FRAME_CYCLE_APPLICATION)
    add_int ("frame_number", frame->gtk_frame_number);
  else
    add_int ("frame_number", frame->frame_number);

  add_double ("begin_ms", ms (frame->begin - origin));
  add_double ("work_ms",
              frame->dispatch_end >= frame->begin
                ? ms (frame->dispatch_end - frame->begin)
                : NAN);
  add_double ("presentation_ms", frame->presentation > 0 ? ms (frame->presentation - origin) : NAN);
  add_double ("cycle_to_present_ms",
              exact_presentation (frame)
                ? ms (frame->presentation - frame->begin)
                : NAN);
  add_double ("post_work_wait_ms",
              exact_presentation (frame) && frame->presentation >= frame->dispatch_end
                ? ms (frame->presentation - frame->dispatch_end)
                : NAN);
  add_double ("target_delta_ms",
              exact_presentation (frame) && frame->target_presentation > 0
                ? ms (frame->presentation - frame->target_presentation)
                : NAN);
  add_double ("kms_ready_deadline_delta_ms",
              exact_presentation (frame) && frame->kms_ready > 0 && frame->submission_deadline > 0
                ? ms (frame->kms_ready - frame->submission_deadline)
                : NAN);
  add_double ("update_ms",
              frame->update_begin > 0 && frame->update_end >= frame->update_begin
                ? ms (frame->update_end - frame->update_begin)
                : NAN);
  add_double ("layout_ms",
              frame->layout_begin > 0 && frame->layout_end >= frame->layout_begin
                ? ms (frame->layout_end - frame->layout_begin)
                : NAN);
  add_double ("paint_ms",
              frame->paint_begin > 0 && frame->paint_end >= frame->paint_begin
                ? ms (frame->paint_end - frame->paint_begin)
                : NAN);
  add_bool ("correlation_estimated", frame->correlation_estimated);
  add_bool ("presentation_estimated", frame->presentation_estimated);

  if (frame->matching_compositor >= 0)
    match = sysprof_frame_cycle_get_frame (cycle, frame->matching_compositor);

  if (match != NULL)
    {
      begin_object ("matching_compositor");
      add_string ("basis", "unique shared presentation timestamp; no surface/commit causality");
      add_int ("index", frame->matching_compositor);
      add_string ("kind", "compositor");
      add_int ("pid", match->pid);
      add_string ("lane", sysprof_frame_cycle_get_lane (cycle, match->lane));
      add_int ("lane_index", match->lane);
      match_clock = g_strdup_printf ("0x%x", match->clock_id);
      add_string ("clock", match_clock);
      add_bool ("correlation_estimated", match->correlation_estimated);
      add_bool ("presentation_estimated", match->presentation_estimated);
      add_double ("work_ms",
                  match->dispatch_end >= match->begin
                    ? ms (match->dispatch_end - match->begin)
                    : NAN);
      add_double ("target_delta_ms",
                  exact_presentation (match) && match->target_presentation > 0
                    ? ms (match->presentation - match->target_presentation)
                    : NAN);
      json_builder_end_object (builder);
    }

  json_builder_end_object (builder);
}

static int
compare_streams (gconstpointer a,
                 gconstpointer b)
{
  const FrameStream *x = *(FrameStream *const *)a;
  const FrameStream *y = *(FrameStream *const *)b;

  guint nx = x->results[0] + x->results[1] + x->results[2] + x->results[3];
  guint ny = y->results[0] + y->results[1] + y->results[2] + y->results[3];

  if (nx != ny)
    return (ny > nx) - (ny < nx);

  return g_strcmp0 (x->key, y->key);
}

static void
write_frames (SysprofDocument   *document,
              SysprofFrameCycle *cycle)
{
  g_autoptr(GPtrArray) streams = g_ptr_array_new_with_free_func ((GDestroyNotify)frame_stream_free);
  g_autoptr(GArray) selected = g_array_new (FALSE, FALSE, sizeof (guint));
  guint n_frames = sysprof_frame_cycle_get_n_frames (cycle);
  const char *results[] = { "incomplete", "work_complete", "ready", "presented" };
  guint eligible = 0;
  guint late = 0;

  for (guint i = 0; i < n_frames; i++)
    {
      const SysprofFrameCycleFrame *frame = sysprof_frame_cycle_get_frame (cycle, i);
      FrameStream *stream;

      if (opt_frame >= 0 && i != (guint)opt_frame)
        continue;

      if (opt_process >= 0 && frame->pid != opt_process)
        continue;

      if (!in_window (frame->begin))
        continue;

      stream = find_stream (streams, cycle, frame);
      accumulate_frame (stream, frame);
      g_array_append_val (selected, i);
    }

  g_ptr_array_sort (streams, compare_streams);

  begin_object ("frames");
  add_int ("available", n_frames);
  add_int ("count", selected->len);

  for (guint i = 0; i < streams->len; i++)
    {
      FrameStream *stream = g_ptr_array_index (streams, i);

      eligible += stream->target_eligible;
      late += stream->target_late;
    }

  add_int ("target_eligible", eligible);
  add_int ("target_late", late);
  add_int ("streams_total", streams->len);
  add_int ("streams_omitted", MAX (0, (int)streams->len - opt_top));

  begin_array ("streams");

  for (guint i = 0; i < MIN (streams->len, (guint)opt_top); i++)
    {
      FrameStream *stream = g_ptr_array_index (streams, i);
      g_autoptr(SysprofDocumentProcess) process = sysprof_document_lookup_process (document, stream->pid);
      g_autofree char *title = process ? sysprof_document_process_dup_title (process) : NULL;
      g_autofree char *clock = g_strdup_printf ("0x%" G_GINT64_MODIFIER "x", stream->clock);

      guint intervals = 0;
      gint64 first = 0;
      gint64 last = 0;

      begin_object (NULL);
      add_string ("kind",
                  stream->kind == SYSPROF_FRAME_CYCLE_APPLICATION
                    ? "application"
                    : "compositor");
      add_int ("pid", stream->pid);
      add_string ("lane", sysprof_frame_cycle_get_lane (cycle, stream->lane));
      add_int ("lane_index", stream->lane);
      add_string ("clock", clock);
      add_string ("process", title);
      add_int ("count", stream->results[0] + stream->results[1] + stream->results[2] + stream->results[3]);
      add_int ("presentation_observed", stream->observed);
      add_int ("presentation_estimated", stream->estimated);
      add_int ("correlation_estimated", stream->correlation_estimated);
      add_int ("inconsistent_timing", stream->inconsistent);
      add_int ("presentation_before_work_end", stream->presentation_before_work_end);
      add_int ("target_eligible", stream->target_eligible);
      add_int ("target_late", stream->target_late);
      add_double ("target_late_pct",
                  stream->target_eligible
                    ? 100.0 * stream->target_late / stream->target_eligible
                    : NAN);
      add_int ("work_over_budget", stream->work_over_budget);

      begin_object ("results");

      for (guint j = 0; j < 4; j++)
        add_int (results[j], stream->results[j]);

      json_builder_end_object (builder);

      g_array_sort (stream->presentations, compare_int64);

      for (guint j = 1; j < stream->presentations->len; j++)
        {
          gint64 prev = g_array_index (stream->presentations, gint64, j - 1);
          gint64 next = g_array_index (stream->presentations, gint64, j);

          if (next > prev)
            {
              append_duration (stream->metrics[7], prev, next);
              intervals++;
            }
          else
            {
              stream->duplicate_presentations++;
            }
        }

      if (stream->presentations->len > 0)
        {
          first = g_array_index (stream->presentations, gint64, 0);
          last = g_array_index (stream->presentations, gint64, stream->presentations->len - 1);
        }

      add_double ("observed_presented_hz", intervals && last > first ? intervals * 1e9 / (last - first) : NAN);
      add_double ("presentation_span_ms", intervals ? ms (last - first) : NAN);
      add_int ("duplicate_presentations", stream->duplicate_presentations);

      for (guint j = 0; j < G_N_ELEMENTS (metric_names); j++)
        write_distribution (metric_names[j], stream->metrics[j]);

      json_builder_end_object (builder);
    }

  json_builder_end_array (builder);

  begin_array ("examples");

  if (opt_frame >= 0)
    {
      write_frame_example (cycle, opt_frame, "requested");
    }
  else
    {
      g_autoptr(GHashTable) used = g_hash_table_new (g_direct_hash, g_direct_equal);
      const char *reasons[] = { "target_late", "work_elapsed", "cycle_to_present" };

      for (guint k = 0; k < MIN ((guint)opt_worst, selected->len); k++)
        {
          gint64 best = G_MININT64;
          guint best_index = G_MAXUINT;
          guint metric = k % 3;

          for (guint attempt = 0; attempt < 3 && best_index == G_MAXUINT; attempt++)
            {
              metric = (k + attempt) % 3;
              best = G_MININT64;

              for (guint j = 0; j < selected->len; j++)
                {
                  guint index = g_array_index (selected, guint, j);

                  const SysprofFrameCycleFrame *frame = sysprof_frame_cycle_get_frame (cycle, index);
                  gint64 value = G_MININT64;

                  if (g_hash_table_contains (used, GUINT_TO_POINTER (index + 1)))
                    continue;

                  if (metric == 0 &&
                      frame->kind == SYSPROF_FRAME_CYCLE_COMPOSITOR &&
                      exact_presentation (frame) &&
                      frame->target_presentation > 0)
                    value = frame->presentation - frame->target_presentation;
                  else if (metric == 1 && frame->dispatch_end >= frame->begin)
                    value = frame->dispatch_end - frame->begin;
                  else if (metric == 2 && exact_presentation (frame))
                    value = frame->presentation - frame->begin;

                  if (value > best)
                    {
                      best = value;
                      best_index = index;
                    }
                }
            }

          if (best_index == G_MAXUINT)
            {
              for (guint j = 0; j < selected->len; j++)
                {
                  guint index = g_array_index (selected, guint, j);

                  if (!g_hash_table_contains (used, GUINT_TO_POINTER (index + 1)))
                    {
                      best_index = index;
                      break;
                    }
                }

              if (best_index == G_MAXUINT)
                break;
            }

          g_hash_table_add (used, GUINT_TO_POINTER (best_index + 1));
          write_frame_example (cycle, best_index, reasons[metric]);
        }
    }

  json_builder_end_array (builder);

  add_int ("examples_omitted",
           opt_frame >= 0
             ? selected->len > 0 ? selected->len - 1 : 0
             : MAX (0, (int)selected->len - opt_worst));
  json_builder_end_object (builder);
}

static void
mark_group_free (MarkGroup *group)
{
  g_free (group->group);
  g_free (group->name);
  g_array_unref (group->durations);
  g_free (group);
}

static int
compare_marks (gconstpointer a,
               gconstpointer b)
{
  const MarkGroup *x = *(MarkGroup *const *)a;
  const MarkGroup *y = *(MarkGroup *const *)b;
  long double sx = 0;
  long double sy = 0;

  for (guint i = 0; i < x->durations->len; i++)
    sx += g_array_index (x->durations, gint64, i);

  for (guint i = 0; i < y->durations->len; i++)
    sy += g_array_index (y->durations, gint64, i);

  if (sx != sy)
    return (sy > sx) - (sy < sx);

  if (x->pid != y->pid)
    return (x->pid > y->pid) - (x->pid < y->pid);

  if (g_strcmp0 (x->group, y->group) != 0)
    return g_strcmp0 (x->group, y->group);

  return g_strcmp0 (x->name, y->name);
}

static void
write_marks (SysprofDocument *document)
{
  g_autoptr(GListModel) model = sysprof_document_list_marks (document);
  g_autoptr(GPtrArray) groups = g_ptr_array_new_with_free_func ((GDestroyNotify)mark_group_free);
  guint count = 0;
  guint points = 0;
  guint scheduler = 0;
  guint invalid = 0;

  for (guint i = 0; i < g_list_model_get_n_items (model); i++)
    {
      g_autoptr(SysprofDocumentMark) mark = g_list_model_get_item (model, i);
      int pid = sysprof_document_frame_get_pid (SYSPROF_DOCUMENT_FRAME (mark));
      gint64 time = sysprof_document_frame_get_time (SYSPROF_DOCUMENT_FRAME (mark));
      const char *group = sysprof_document_mark_get_group (mark);
      const char *name = sysprof_document_mark_get_name (mark);
      gint64 duration = sysprof_document_mark_get_duration (mark);
      MarkGroup *entry = NULL;

      if ((opt_process >= 0 && pid != opt_process) || !in_window (time))
        continue;

      if (g_strcmp0 (group, "Process State") == 0)
        {
          scheduler++;
          continue;
        }

      count++;

      for (guint j = 0; j < groups->len; j++)
        {
          MarkGroup *candidate = g_ptr_array_index (groups, j);

          if (candidate->pid == pid &&
              g_strcmp0 (candidate->group, group) == 0 &&
              g_strcmp0 (candidate->name, name) == 0)
            {
              entry = candidate;
              break;
            }
        }

      if (entry == NULL)
        {
          entry = g_new0 (MarkGroup, 1);
          entry->pid = pid;
          entry->group = g_strdup (group);
          entry->name = g_strdup (name);
          entry->durations = g_array_new (FALSE, FALSE, sizeof (gint64));
          g_ptr_array_add (groups, entry);
        }

      if (duration > 0)
        g_array_append_val (entry->durations, duration);
      else if (duration == 0)
        {
          entry->points++;
          points++;
        }
      else
        {
          invalid++;
        }
    }

  g_ptr_array_sort (groups, compare_marks);

  begin_object ("marks");
  add_string ("scope", "PID/time filtered starts; full durations; overlapping marks are not exclusive CPU time");
  add_int ("count", count);
  add_int ("point_count", points);
  add_int ("invalid_duration_count", invalid);
  add_int ("scheduler_mark_count", scheduler);
  add_int ("group_count", groups->len);
  add_int ("omitted_groups", MAX (0, (int)groups->len - opt_top));

  begin_array ("groups");

  for (guint i = 0; i < MIN (groups->len, (guint)opt_top); i++)
    {
      MarkGroup *entry = g_ptr_array_index (groups, i);

      begin_object (NULL);
      add_int ("pid", entry->pid);
      add_string ("group", entry->group);
      add_string ("name", entry->name);
      add_int ("points", entry->points);
      write_distribution ("duration", entry->durations);
      json_builder_end_object (builder);
    }

  json_builder_end_array (builder);
  json_builder_end_object (builder);
}

static void
write_scheduler (SysprofDocument *document)
{
  g_autoptr(GListModel) model = sysprof_document_list_marks_by_group (document, "Process State");
  g_autoptr(GHashTable) totals = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  GList *keys = NULL;
  guint count = 0;

  for (guint i = 0; i < g_list_model_get_n_items (model); i++)
    {
      g_autoptr(SysprofDocumentMark) mark = g_list_model_get_item (model, i);
      int tid = sysprof_document_frame_get_pid (SYSPROF_DOCUMENT_FRAME (mark));
      gint64 begin = sysprof_document_frame_get_time (SYSPROF_DOCUMENT_FRAME (mark));
      gint64 end = sysprof_document_mark_get_end_time (mark);
      g_autofree char *id = NULL;
      gint64 *total;

      if ((opt_process >= 0 && tid != opt_process) || end <= selection_begin || begin >= selection_end)
        continue;

      id = g_strdup_printf ("%d:%s", tid, sysprof_document_mark_get_name (mark));

      if (!(total = g_hash_table_lookup (totals, id)))
        {
          total = g_new0 (gint64, 2);
          g_hash_table_insert (totals, g_steal_pointer (&id), total);
        }

      total[0] += MAX (0, MIN (end, selection_end) - MAX (begin, selection_begin));
      total[1]++;
    }

  begin_object ("scheduler_states");
  add_string ("scope", "Process State marks use TID as PID; intervals clipped to window; missing coverage is unknown");
  add_int ("group_count", g_hash_table_size (totals));
  add_int ("omitted_groups", MAX (0, (int)g_hash_table_size (totals) - opt_top));

  begin_array ("groups");
  keys = g_list_sort (g_hash_table_get_keys (totals), (GCompareFunc)g_strcmp0);

  for (GList *iter = keys; iter != NULL && count < opt_top; iter = iter->next)
    {
      char **parts = g_strsplit (iter->data, ":", 2);
      gint64 *total = g_hash_table_lookup (totals, iter->data);

      begin_object (NULL);
      add_int ("recorded_tid", atoi (parts[0]));
      add_string ("state", parts[1]);
      add_int ("observations", total[1]);
      add_double ("overlap_ms", ms (total[0]));
      json_builder_end_object (builder);

      g_strfreev (parts);
      count++;
    }

  g_list_free (keys);
  json_builder_end_array (builder);
  json_builder_end_object (builder);
}

static void
write_samples (SysprofDocument *document)
{
  g_autoptr(GListModel) model = sysprof_document_list_samples (document);
  g_autoptr(GPtrArray) threads = g_ptr_array_new_with_free_func (g_free);
  g_autoptr(GPtrArray) leaves = g_ptr_array_new_with_free_func ((GDestroyNotify)leaf_summary_free);
  g_autoptr(GHashTable) leaves_by_key = g_hash_table_new_full (g_str_hash,
                                                               g_str_equal,
                                                               g_free,
                                                               NULL);
  guint count = 0;
  guint empty = 0;
  guint unresolved = 0;
  guint fallback = 0;
  guint kinds[4] = { 0 };

  for (guint i = 0; i < g_list_model_get_n_items (model); i++)
    {
      g_autoptr(SysprofDocumentSample) sample = g_list_model_get_item (model, i);
      SysprofDocumentFrame *frame = SYSPROF_DOCUMENT_FRAME (sample);
      int pid = sysprof_document_frame_get_pid (frame);
      int tid = sysprof_document_traceable_get_thread_id (SYSPROF_DOCUMENT_TRACEABLE (sample));
      int cpu = sysprof_document_frame_get_cpu (frame);
      SysprofCpuKind kind = sysprof_document_get_cpu_kind (document, cpu);
      ThreadSummary *thread = NULL;
      SysprofDocumentTraceable *traceable = SYSPROF_DOCUMENT_TRACEABLE (sample);
      guint depth;

      if ((opt_process >= 0 && pid != opt_process) || !in_window (sysprof_document_frame_get_time (frame)))
        continue;

      count++;
      depth = sysprof_document_traceable_get_stack_depth (traceable);

      if (depth == 0)
        empty++;

      kinds[kind]++;

      for (guint j = 0; j < threads->len; j++)
        {
          ThreadSummary *candidate = g_ptr_array_index (threads, j);

          if (candidate->pid == pid && candidate->tid == tid)
            {
              thread = candidate;
              break;
            }
        }

      if (thread == NULL)
        {
          thread = g_new0 (ThreadSummary, 1);
          thread->pid = pid;
          thread->tid = tid;
          g_ptr_array_add (threads, thread);
        }

      thread->count++;
      thread->cpu_kind[kind]++;

      if (depth > 0)
        {
          guint64 addresses[8] = { 0 };
          guint positions[8] = { 0 };
          guint n_addresses = 0;
          guint physical_depth = 0;
          SysprofSymbol *resolved;
          const char *name;
          const char *binary;
          gboolean is_fallback;
          g_autofree char *raw_name = NULL;
          g_autofree char *key = NULL;
          LeafSummary *leaf = NULL;

          for (guint j = 0; j < depth; j++)
            {
              guint64 address = sysprof_document_traceable_get_stack_address (traceable, j);

              if (sysprof_address_is_context_switch (address, NULL))
                continue;

              if (n_addresses < G_N_ELEMENTS (addresses))
                {
                  addresses[n_addresses] = address;
                  positions[n_addresses] = j;
                  n_addresses++;
                }

              physical_depth++;
            }

          if (n_addresses == 0)
            {
              empty++;
              continue;
            }

          resolved = opt_no_symbols ? NULL :
            resolve_physical_address (document, traceable, positions[0]);
          name = resolved ? sysprof_symbol_get_name (resolved) : NULL;
          binary = resolved ? sysprof_symbol_get_binary_nick (resolved) : NULL;
          is_fallback = resolved && resolved->is_fallback;

          if (name == NULL)
            {
              raw_name = g_strdup_printf ("0x%" G_GINT64_MODIFIER "x", addresses[0]);
              name = raw_name;
              unresolved++;
            }
          else if (is_fallback)
            {
              fallback++;
            }

          key = g_strdup_printf ("%d\x1f%s\x1f%s", pid, name, binary ? : "");
          leaf = g_hash_table_lookup (leaves_by_key, key);

          if (leaf == NULL)
            {
              leaf = g_new0 (LeafSummary, 1);
              leaf->pid = pid;
              leaf->name = g_strdup (name);
              leaf->binary = g_strdup (binary);
              leaf->time = sysprof_document_frame_get_time (frame);
              leaf->depth = n_addresses;
              leaf->raw_depth = physical_depth;
              leaf->representative = g_object_ref (sample);

              for (guint j = 0; j < n_addresses; j++)
                {
                  leaf->addresses[j] = addresses[j];
                  leaf->positions[j] = positions[j];
                }

              if (resolved)
                leaf->symbols[0] = g_strdup (sysprof_symbol_get_name (resolved));

              g_ptr_array_add (leaves, leaf);
              g_hash_table_insert (leaves_by_key, g_steal_pointer (&key), leaf);
            }

          leaf->count++;
        }
    }

  begin_object ("samples");
  add_string ("scope", "PID/time filtered; counts are samples, not CPU time");
  add_int ("count", count);
  add_int ("empty_stacks", empty);
  add_int ("unresolved_leaf_count", unresolved);
  add_int ("fallback_leaf_count", fallback);
  add_string ("symbolization", opt_no_symbols ? "disabled" : "enabled");
  add_int ("thread_count", threads->len);
  add_int ("omitted_threads", MAX (0, (int)threads->len - opt_top));

  g_ptr_array_sort (leaves, compare_leaves);

  for (guint i = 0; i < MIN (leaves->len, (guint)opt_top); i++)
    resolve_stack (document, g_ptr_array_index (leaves, i));

  add_int ("symbol_count", leaves->len);
  add_int ("omitted_symbols", MAX (0, (int)leaves->len - opt_top));

  begin_object ("cpu_kind_samples");
  add_int ("unknown", kinds[0]);
  add_int ("performance", kinds[1]);
  add_int ("efficiency", kinds[2]);
  add_int ("low_power_efficiency", kinds[3]);
  json_builder_end_object (builder);

  begin_array ("threads");

  for (guint i = 0; i < MIN (threads->len, (guint)opt_top); i++)
    {
      ThreadSummary *thread = g_ptr_array_index (threads, i);
      g_autoptr(SysprofDocumentProcess) process = sysprof_document_lookup_process (document, thread->pid);
      g_autofree char *title = process ? sysprof_document_process_dup_title (process) : NULL;

      begin_object (NULL);
      add_int ("pid", thread->pid);
      add_int ("tid", thread->tid);
      add_string ("process", title);
      add_int ("samples", thread->count);
      add_bool ("main_thread", thread->pid == thread->tid);

      begin_object ("cpu_kind_samples");
      add_int ("unknown", thread->cpu_kind[0]);
      add_int ("performance", thread->cpu_kind[1]);
      add_int ("efficiency", thread->cpu_kind[2]);
      add_int ("low_power_efficiency", thread->cpu_kind[3]);
      json_builder_end_object (builder);
      json_builder_end_object (builder);
    }

  json_builder_end_array (builder);

  begin_array ("hot_leaf_symbols");

  for (guint i = 0; i < MIN (leaves->len, (guint)opt_top); i++)
    {
      LeafSummary *leaf = g_ptr_array_index (leaves, i);

      begin_object (NULL);
      add_int ("pid", leaf->pid);
      add_string ("symbol", leaf->name);
      add_string ("binary", leaf->binary);
      add_int ("samples", leaf->count);
      add_double ("share_pct", count ? leaf->count * 100.0 / count : NAN);
      add_source_location (leaf->locations[0]);
      add_double ("representative_time_ms", ms (leaf->time - origin));
      add_int ("raw_address_depth", leaf->raw_depth);
      add_bool ("stack_truncated", leaf->raw_depth > leaf->depth);

      begin_array ("representative_stack_leaf_first");

      for (guint j = 0; j < leaf->depth; j++)
        {
          g_autofree char *address = g_strdup_printf ("0x%" G_GINT64_MODIFIER "x", leaf->addresses[j]);

          begin_object (NULL);
          add_string ("address", address);
          add_string ("symbol", leaf->symbols[j]);
          add_string ("resolution", leaf->symbols[j] ?
                      leaf->fallback_addresses[j] ? "fallback" : "symbol" : "unresolved");
          add_source_location (leaf->locations[j]);
          json_builder_end_object (builder);
        }

      json_builder_end_array (builder);
      json_builder_end_object (builder);
    }

  json_builder_end_array (builder);
  json_builder_end_object (builder);
}

static gboolean
counter_selected (SysprofDocumentCounter *counter)
{
  const char *category = sysprof_document_counter_get_category (counter);
  const char *name = sysprof_document_counter_get_name (counter);
  g_autofree char *label = g_strconcat (category ? category : "", " ", name ? name : "", NULL);
  g_autofree char *haystack = g_utf8_strdown (label, -1);
  g_autofree char *needle = opt_counter ? g_utf8_strdown (opt_counter, -1) : NULL;

  if (needle != NULL)
    return strstr (haystack, needle) != NULL;

  return strstr (haystack, "combined") || strstr (haystack, "pressure") ||
         strstr (haystack, "available memory") || strstr (haystack, "rapl");
}

static void
write_counters (SysprofDocument *document)
{
  g_autoptr(GListModel) model = sysprof_document_list_counters (document);
  guint matched = 0;
  guint emitted = 0;

  begin_object ("counters");
  add_string ("scope",
              "Global counters, time filtered; sample means are not time weighted; "
              "int64 converted to double; model normalizes doubles above G_MAXINT64, "
              "including +Inf, to zero");
  add_string ("filter", opt_counter ? opt_counter : "curated CPU/pressure/available-memory/RAPL");
  add_int ("available", g_list_model_get_n_items (model));

  begin_array ("items");

  for (guint i = 0; i < g_list_model_get_n_items (model); i++)
    {
      g_autoptr(SysprofDocumentCounter) counter = g_list_model_get_item (model, i);
      guint samples = 0;
      guint nonfinite = 0;
      double first = NAN, last = NAN, minimum = INFINITY, maximum = -INFINITY, sum = 0;

      gint64 first_time = 0, last_time = 0;
      gboolean is_int = sysprof_document_counter_get_value_type (counter) == G_TYPE_INT64;

      if (!counter_selected (counter))
        continue;

      matched++;

      if (emitted >= opt_top)
        continue;

      for (guint j = 0; j < sysprof_document_counter_get_n_values (counter); j++)
        {
          gint64 time;
          double val = is_int
                     ? (double)sysprof_document_counter_get_value_int64 (counter, j, &time)
                     : sysprof_document_counter_get_value_double (counter, j, &time);

          if (!in_window (time))
            continue;

          if (!isfinite (val))
            {
              nonfinite++;
              continue;
            }

          if (samples == 0)
            {
              first = val;
              first_time = time;
            }

          last = val;
          last_time = time;
          minimum = MIN (minimum, val);
          maximum = MAX (maximum, val);
          sum += val;
          samples++;
        }

      begin_object (NULL);
      add_int ("id", sysprof_document_counter_get_id (counter));
      add_string ("category", sysprof_document_counter_get_category (counter));
      add_string ("name", sysprof_document_counter_get_name (counter));
      add_string ("description", sysprof_document_counter_get_description (counter));
      add_string ("value_type", is_int ? "int64 converted to double" : "double");
      add_int ("samples", samples);
      add_int ("nonfinite_samples", nonfinite);
      add_double ("first", first);
      add_double ("last", last);
      add_double ("min", minimum);
      add_double ("max", maximum);
      add_double ("sample_mean", samples ? sum / samples : NAN);
      add_double ("first_ms", samples ? ms (first_time - origin) : NAN);
      add_double ("last_ms", samples ? ms (last_time - origin) : NAN);
      json_builder_end_object (builder);

      emitted++;
    }

  json_builder_end_array (builder);

  add_int ("matched", matched);
  add_int ("omitted", matched - emitted);
  json_builder_end_object (builder);
}

static void
write_diagnostics (SysprofDocument *document)
{
  g_autoptr(GListModel) model = sysprof_document_list_diagnostics (document);
  guint count = 0;
  guint emitted = 0;

  begin_object ("diagnostics");
  add_string ("scope", "Document-wide PID scope; overlapping intervals; provider output may already be capped");
  add_int ("provider_items", g_list_model_get_n_items (model));

  begin_array ("items");

  for (guint i = 0; i < g_list_model_get_n_items (model); i++)
    {
      g_autoptr(SysprofDiagnostic) item = g_list_model_get_item (model, i);
      const SysprofTimeSpan *span = sysprof_diagnostic_get_time_span (item);
      g_autoptr(GListModel) evidence = NULL;
      g_autofree char *message = NULL;

      if (span && (span->end_nsec < selection_begin || span->begin_nsec >= selection_end))
        continue;

      count++;

      if (emitted >= opt_top)
        continue;

      evidence = sysprof_diagnostic_list_frames (item);
      message = g_utf8_make_valid (sysprof_diagnostic_get_message (item) ? : "", -1);

      if (g_utf8_strlen (message, -1) > 320)
        {
          char *end = g_utf8_offset_to_pointer (message, 309);

          *end = '\0';
          message = g_realloc (message, strlen (message) + 12);
          strcat (message, "[truncated]");
        }

      begin_object (NULL);
      add_string ("domain", sysprof_diagnostic_get_domain (item));
      add_string ("category", sysprof_diagnostic_get_category (item));
      add_string ("message", message);
      add_double ("begin_ms", span ? ms (span->begin_nsec - origin) : NAN);
      add_double ("end_ms", span ? ms (span->end_nsec - origin) : NAN);
      add_int ("evidence_frames", evidence ? g_list_model_get_n_items (evidence) : 0);
      json_builder_end_object (builder);

      emitted++;
    }

  json_builder_end_array (builder);

  add_int ("count", count);
  add_int ("omitted", count - emitted);
  json_builder_end_object (builder);
}

static void
write_logs (SysprofDocument *document)
{
  g_autoptr(GListModel) model = sysprof_document_list_logs (document);
  guint count = 0;
  guint emitted = 0;

  begin_object ("logs");
  add_string ("scope", "PID/time filtered warning, critical and error records");

  begin_array ("items");

  for (guint i = 0; i < g_list_model_get_n_items (model); i++)
    {
      g_autoptr(SysprofDocumentLog) log = g_list_model_get_item (model, i);
      SysprofDocumentFrame *frame = SYSPROF_DOCUMENT_FRAME (log);
      GLogLevelFlags level = sysprof_document_log_get_severity (log);
      g_autofree char *message = NULL;

      if ((opt_process >= 0 && sysprof_document_frame_get_pid (frame) != opt_process) ||
          !in_window (sysprof_document_frame_get_time (frame)))
        continue;

      if (!(level & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING)))
        continue;

      count++;

      if (emitted >= opt_top)
        continue;

      message = g_utf8_make_valid (sysprof_document_log_get_message (log) ? : "", -1);

      if (g_utf8_strlen (message, -1) > 320)
        {
          char *end = g_utf8_offset_to_pointer (message, 309);

          *end = '\0';
          message = g_realloc (message, strlen (message) + 12);
          strcat (message, "[truncated]");
        }

      begin_object (NULL);
      add_int ("pid", sysprof_document_frame_get_pid (frame));
      add_double ("time_ms", ms (sysprof_document_frame_get_time (frame) - origin));
      add_string ("severity",
                  level & G_LOG_LEVEL_ERROR
                    ? "ERROR"
                    : level & G_LOG_LEVEL_CRITICAL ? "CRITICAL" : "WARNING");
      add_string ("domain", sysprof_document_log_get_domain (log));
      add_string ("message", message);
      json_builder_end_object (builder);

      emitted++;
    }

  json_builder_end_array (builder);

  add_int ("count", count);
  add_int ("omitted", count - emitted);
  json_builder_end_object (builder);
}

static void
write_inventory (SysprofDocument *document)
{
  g_autoptr(GListModel) processes = sysprof_document_list_processes (document);
  g_autoptr(GListModel) allocations = sysprof_document_list_allocations (document);
  g_autoptr(GListModel) dbus = sysprof_document_list_dbus_messages (document);
  g_autoptr(GListModel) metadata = sysprof_document_list_metadata (document);

  begin_object ("inventory");
  add_string ("scope", "Whole capture");
  add_int ("processes", g_list_model_get_n_items (processes));
  add_int ("allocations", g_list_model_get_n_items (allocations));
  add_int ("dbus_messages", g_list_model_get_n_items (dbus));
  add_int ("metadata", g_list_model_get_n_items (metadata));
  json_builder_end_object (builder);
}

static gboolean
parse_options (int    *argc,
               char ***argv)
{
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GError) error = NULL;

  GOptionEntry entries[] = {
    { "json", 0, 0, G_OPTION_ARG_NONE, &opt_json, "Output JSON", NULL },
    { "process", 'p', 0, G_OPTION_ARG_INT, &opt_process, "Filter PID", "PID" },
    { "from", 0, 0, G_OPTION_ARG_DOUBLE, &opt_from, "Start seconds", "S" },
    { "to", 0, 0, G_OPTION_ARG_DOUBLE, &opt_to, "End seconds", "S" },
    { "frame", 0, 0, G_OPTION_ARG_INT, &opt_frame, "Inspect frame index", "INDEX" },
    { "top", 'n', 0, G_OPTION_ARG_INT, &opt_top, "Rows per table", "N" },
    { "worst", 0, 0, G_OPTION_ARG_INT, &opt_worst, "Frame examples", "N" },
    { "budget-ms", 0, 0, G_OPTION_ARG_DOUBLE, &opt_budget, "Work budget", "MS" },
    { "counter", 0, 0, G_OPTION_ARG_STRING, &opt_counter, "Counter substring", "TEXT" },
    { "no-symbols", 0, 0, G_OPTION_ARG_NONE, &opt_no_symbols, "Disable symbolization", NULL },
    { NULL }
  };

  context = g_option_context_new ("CAPTURE_FILE");
  g_option_context_add_main_entries (context, entries, NULL);

  if (!g_option_context_parse (context, argc, argv, &error))
    {
      g_printerr ("%s\n", error->message);

      return FALSE;
    }

  if (*argc != 2 || opt_process < -1 || opt_frame < -1 || opt_top < 1 || opt_top > 100 ||
      opt_worst < 0 || opt_worst > 20 || (opt_from != -1 && (!isfinite (opt_from) || opt_from < 0)) ||
      (opt_to != -1 && (!isfinite (opt_to) || opt_to < 0)) ||
      (opt_from >= 0 && opt_to >= 0 && opt_from >= opt_to) ||
      (opt_budget != -1 && (!isfinite (opt_budget) || opt_budget <= 0)) ||
      (opt_frame >= 0 && (opt_from >= 0 || opt_to >= 0)))
    {
      g_printerr ("Invalid arguments. Use --help for usage.\n");

      return FALSE;
    }

  return TRUE;
}

static void
render_text_object (GString    *text,
                    JsonObject *object,
                    const char *name,
                    guint       depth)
{
  GList *members = json_object_get_members (object);
  gboolean first = TRUE;

  if (json_object_has_member (object, "n") &&
      json_object_get_int_member (object, "n") == 0)
    {
      g_list_free (members);

      return;
    }

  g_string_append_printf (text, "%*s%s:", depth * 2, "", name);

  for (GList *iter = members; iter != NULL; iter = iter->next)
    {
      const char *key = iter->data;
      JsonNode *node = json_object_get_member (object, key);
      g_autofree char *value = NULL;

      if (JSON_NODE_TYPE (node) == JSON_NODE_OBJECT || JSON_NODE_TYPE (node) == JSON_NODE_ARRAY)
        continue;

      if (JSON_NODE_HOLDS_VALUE (node) && json_node_get_value_type (node) == G_TYPE_DOUBLE)
        {
          char number[G_ASCII_DTOSTR_BUF_SIZE];

          g_ascii_formatd (number, sizeof number, "%.3f", json_node_get_double (node));
          value = g_strdup (number);
        }
      else
        {
          value = json_to_string (node, FALSE);
        }

      g_string_append_printf (text, "%s%s=%s", first ? " " : " ", key, value);
      first = FALSE;
    }

  g_string_append_c (text, '\n');

  for (GList *iter = members; iter != NULL; iter = iter->next)
    {
      const char *key = iter->data;
      JsonNode *node = json_object_get_member (object, key);

      if (JSON_NODE_TYPE (node) == JSON_NODE_OBJECT)
        {
          render_text_object (text, json_node_get_object (node), key, depth + 1);
        }
      else if (JSON_NODE_TYPE (node) == JSON_NODE_ARRAY)
        {
          JsonArray *array = json_node_get_array (node);

          for (guint i = 0; i < json_array_get_length (array); i++)
            {
              JsonNode *item = json_array_get_element (array, i);

              if (JSON_NODE_TYPE (item) == JSON_NODE_OBJECT)
                {
                  render_text_object (text, json_node_get_object (item), key, depth + 1);
                }
              else
                {
                  g_autofree char *value = json_to_string (item, FALSE);

                  g_string_append_printf (text, "%*s%s: %s\n", (depth + 1) * 2, "", key, value);
                }
            }
        }
    }

  g_list_free (members);
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr(SysprofDocumentLoader) loader = NULL;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(SysprofFrameCycle) cycle = NULL;
  g_autoptr(SysprofMultiSymbolizer) multi = NULL;
  g_autoptr(JsonGenerator) generator = NULL;
  g_autoptr(JsonNode) root = NULL;
  g_autoptr(GString) text_report = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *output = NULL;
  g_autofree char *basename = NULL;
  const SysprofTimeSpan *span;

  sysprof_clock_init ();
  dex_init ();

  if (!parse_options (&argc, &argv))
    return 2;

  loader = sysprof_document_loader_new (argv[1]);

  if (opt_no_symbols)
    {
      sysprof_document_loader_set_symbolizer (loader, sysprof_no_symbolizer_get ());
    }
  else
    {
      multi = sysprof_multi_symbolizer_new ();
      sysprof_multi_symbolizer_take (multi, sysprof_bundled_symbolizer_new ());
      sysprof_multi_symbolizer_take (multi, sysprof_kallsyms_symbolizer_new ());
      sysprof_multi_symbolizer_take (multi, sysprof_elf_symbolizer_new ());
      sysprof_multi_symbolizer_take (multi, sysprof_jitmap_symbolizer_new ());
      sysprof_document_loader_set_symbolizer (loader, SYSPROF_SYMBOLIZER (multi));
    }

  if (!(document = sysprof_document_loader_load (loader, NULL, &error)))
    {
      g_printerr ("Failed to load capture: %s\n", error->message);

      return 1;
    }

  cycle = sysprof_frame_cycle_new (document);

  if (opt_frame >= 0 && (guint)opt_frame >= sysprof_frame_cycle_get_n_frames (cycle))
    {
      g_printerr ("Frame index %d is out of range.\n", opt_frame);

      return 2;
    }

  span = sysprof_document_get_time_span (document);
  origin = span->begin_nsec;

  if ((opt_from >= 0 && opt_from >= (G_MAXINT64 - origin - 1) / 1e9) ||
      (opt_to >= 0 && opt_to >= (G_MAXINT64 - origin - 1) / 1e9))
    {
      g_printerr ("Selected time is out of range.\n");

      return 2;
    }

  selection_begin = opt_from >= 0 ? origin + opt_from * 1e9 : origin;
  selection_end = opt_to >= 0 ? origin + opt_to * 1e9 :
                  span->end_nsec < G_MAXINT64 ? span->end_nsec + 1 : G_MAXINT64;

  if (opt_frame >= 0)
    {
      const SysprofFrameCycleFrame *frame = sysprof_frame_cycle_get_frame (cycle, opt_frame);

      selection_begin = frame->begin;
      selection_end = MAX (frame->dispatch_end, frame->presentation);

      if (selection_end < G_MAXINT64)
        selection_end++;

      opt_process = frame->pid;
    }

  builder = json_builder_new ();

  begin_object (NULL);
  add_int ("schema_version", 1);
  basename = g_path_get_basename (argv[1]);
  add_string ("capture", basename);

  begin_object ("scope");
  add_int ("origin_ns", origin);
  add_double ("capture_duration_ms", ms (span->end_nsec - origin));
  add_double ("from_ms", ms (selection_begin - origin));
  add_double ("to_ms", ms (selection_end - origin));
  add_int ("process", opt_process);
  add_int ("frame", opt_frame);
  add_double ("work_budget_ms", opt_budget > 0 ? opt_budget : NAN);
  add_string ("symbols", opt_no_symbols ? "disabled" : "bundled and local; no debuginfod");
  json_builder_end_object (builder);

  begin_array ("semantics");
  json_builder_add_string_value (builder, "Work is elapsed wall time, not CPU time.");
  json_builder_add_string_value (builder, "Cycle-to-present is not input latency; post-work wait is not GPU time.");
  json_builder_add_string_value (builder, "Missing presentation does not prove a dropped frame.");
  json_builder_add_string_value (builder, "Observed cadence includes idle gaps and missing observations.");
  json_builder_add_string_value (builder,
                                 "Zero or absent clock IDs disable cadence because "
                                 "the model cannot distinguish them.");
  json_builder_add_string_value (builder,
                                 "Target lateness requires exact presentation "
                                 "evidence and means delta > 0 without tolerance.");
  json_builder_add_string_value (builder,
                                 "A shared presentation timestamp is not surface "
                                 "or commit identity or proof of causality.");
  json_builder_add_string_value (builder,
                                 "GTK phases may use legacy containment and retain "
                                 "one interval per phase; they are not exclusive CPU costs.");
  json_builder_add_string_value (builder,
                                 "Samples are counts, not CPU time; "
                                 "overlapping marks are not exclusive costs.");
  json_builder_add_string_value (builder,
                                 "Source locations require a recorded ELF map and "
                                 "local debug information; missing locations remain null.");
  json_builder_add_string_value (builder,
                                 "Scheduler switch-out states do not measure "
                                 "wakeup-to-run delay.");
  json_builder_add_string_value (builder,
                                 "Counter means are sample means, not time-weighted "
                                 "means; cumulative values are not integrated.");
  json_builder_end_array (builder);

  write_frames (document, cycle);
  write_inventory (document);
  write_marks (document);
  write_scheduler (document);
  write_samples (document);
  write_counters (document);
  write_diagnostics (document);
  write_logs (document);
  json_builder_end_object (builder);

  root = json_builder_get_root (builder);
  generator = json_generator_new ();
  json_generator_set_root (generator, root);

  if (opt_json)
    {
      output = json_generator_to_data (generator, NULL);
    }
  else
    {
      text_report = g_string_new ("SYSCAP PERFORMANCE SUMMARY v1\n");
      render_text_object (text_report, json_node_get_object (root), "capture_summary", 0);
      output = g_string_free (g_steal_pointer (&text_report), FALSE);
    }

  if (fputs (output, stdout) == EOF || fputc ('\n', stdout) == EOF || fflush (stdout) == EOF)
    {
      g_printerr ("Failed to write report.\n");

      return 1;
    }

  return 0;
}
