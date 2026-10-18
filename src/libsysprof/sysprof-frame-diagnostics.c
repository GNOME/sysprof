/* sysprof-frame-diagnostics.c
 *
 * Copyright 2026 Christian Hergert <christian@sourceandstack.com>
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

#include <glib/gi18n.h>

#include "sysprof-diagnostic-private.h"
#include "sysprof-document-private.h"
#include "sysprof-document-mark.h"
#include "sysprof-frame-diagnostics-private.h"

struct _SysprofFrameDiagnostics
{
  SysprofDiagnosticProvider parent_instance;
};

G_DEFINE_FINAL_TYPE (SysprofFrameDiagnostics, sysprof_frame_diagnostics, SYSPROF_TYPE_DIAGNOSTIC_PROVIDER)

typedef struct
{
  gint64 begin_nsec;
  gint64 end_nsec;
  guint  position;
  int    pid;
} FrameCycle;

typedef struct
{
  SysprofDocument *document;
  GListStore      *diagnostics;
  GArray          *cycles;
  guint            n_slow_frames;
  guint            n_low_power_frames;
} FrameDiagnosticState;

typedef struct
{
  SysprofDocument *document;
  guint            position;
  int              pid;
  int              cpu;
  gboolean         found;
} SampleLookup;

static gboolean
collect_frame_cycle (guint    position,
                     gint64   time,
                     gint64   duration,
                     int      pid,
                     gpointer user_data)
{
  FrameDiagnosticState *state = user_data;
  g_autoptr(SysprofDocumentMark) mark = NULL;
  g_autoptr(SysprofDiagnostic) diagnostic = NULL;
  g_autofree char *message = NULL;
  FrameCycle cycle;
  SysprofTimeSpan span;

  g_assert (state != NULL);

  if (duration < 0 || time > G_MAXINT64 - duration)
    return TRUE;

  cycle = (FrameCycle) { time, time + duration, position, pid };
  g_array_append_val (state->cycles, cycle);

  /* The frame budget is 8,333,333.33... ns, so the first overrun is
   * 8,333,334 ns. Avoid rounding the budget up to a whole nanosecond.
   */
  if (duration <= SYSPROF_NSEC_PER_SEC / 120 ||
      state->n_slow_frames >= SYSPROF_DIAGNOSTIC_PROVIDER_MAX_PER_TYPE)
    return TRUE;

  span.begin_nsec = time;
  span.end_nsec = time + duration;
  mark = g_list_model_get_item (G_LIST_MODEL (state->document), position);
  message = g_strdup_printf (_("Frameclock cycle took %.2f ms (120 Hz budget: 8.33 ms)"),
                             duration / 1000000.);
  diagnostic = g_object_new (SYSPROF_TYPE_DIAGNOSTIC, NULL);
  _sysprof_diagnostic_set_details (diagnostic, "GTK", "frames", message, &span);
  _sysprof_diagnostic_add_frame (diagnostic, SYSPROF_DOCUMENT_FRAME (mark));
  g_list_store_append (state->diagnostics, diagnostic);
  state->n_slow_frames++;

  return TRUE;
}

static gboolean
find_low_power_main_thread_sample (guint    position,
                                   gint64   time,
                                   int      pid,
                                   int      tid,
                                   int      cpu,
                                   gpointer user_data)
{
  SampleLookup *lookup = user_data;

  g_assert (lookup != NULL);

  if (pid == lookup->pid &&
      tid == pid &&
      sysprof_document_get_cpu_kind (lookup->document, cpu) == SYSPROF_CPU_KIND_LOW_POWER_EFFICIENCY)
    {
      lookup->position = position;
      lookup->cpu = cpu;
      lookup->found = TRUE;
      return FALSE;
    }

  return TRUE;
}

static void
sysprof_frame_diagnostics_populate (SysprofDiagnosticProvider *provider,
                                    SysprofDocument           *document,
                                    GListStore                *diagnostics)
{
  g_autoptr(GArray) cycles = g_array_new (FALSE, FALSE, sizeof (FrameCycle));
  FrameDiagnosticState state = { document, diagnostics, cycles, 0, 0 };

  _sysprof_document_foreach_mark (document,
                                  "GTK",
                                  "Frameclock cycle",
                                  collect_frame_cycle,
                                  &state);

  if (cycles->len == 0)
    return;

  for (guint i = 0;
       i < cycles->len && state.n_low_power_frames < SYSPROF_DIAGNOSTIC_PROVIDER_MAX_PER_TYPE;
       i++)
    {
      const FrameCycle *cycle = &g_array_index (cycles, FrameCycle, i);
      SampleLookup lookup = { document, 0, cycle->pid, 0, FALSE };
      g_autoptr(SysprofDocumentFrame) mark = NULL;
      g_autoptr(SysprofDocumentFrame) sample_frame = NULL;
      g_autoptr(SysprofDiagnostic) diagnostic = NULL;
      g_autofree char *message = NULL;
      SysprofTimeSpan span;

      if (cycle->pid <= 0)
        continue;

      _sysprof_document_foreach_sample (document,
                                        cycle->begin_nsec,
                                        cycle->end_nsec,
                                        find_low_power_main_thread_sample,
                                        &lookup);

      if (!lookup.found)
        continue;

      span = (SysprofTimeSpan) { cycle->begin_nsec, cycle->end_nsec };
      mark = g_list_model_get_item (G_LIST_MODEL (document), cycle->position);
      sample_frame = g_list_model_get_item (G_LIST_MODEL (document), lookup.position);
      message = g_strdup_printf (_("Main thread ran on an LP-E core during a frameclock cycle (CPU %d)"),
                                 lookup.cpu);
      diagnostic = g_object_new (SYSPROF_TYPE_DIAGNOSTIC, NULL);
      _sysprof_diagnostic_set_details (diagnostic, "GTK", "frames", message, &span);
      _sysprof_diagnostic_add_frame (diagnostic, mark);
      _sysprof_diagnostic_add_frame (diagnostic, sample_frame);
      g_list_store_append (diagnostics, diagnostic);
      state.n_low_power_frames++;
    }
}

static void
sysprof_frame_diagnostics_class_init (SysprofFrameDiagnosticsClass *klass)
{
  SysprofDiagnosticProviderClass *provider_class = SYSPROF_DIAGNOSTIC_PROVIDER_CLASS (klass);

  provider_class->populate = sysprof_frame_diagnostics_populate;
}

static void
sysprof_frame_diagnostics_init (SysprofFrameDiagnostics *self)
{
}

SysprofDiagnosticProvider *
_sysprof_frame_diagnostics_new (void)
{
  return g_object_new (SYSPROF_TYPE_FRAME_DIAGNOSTICS, NULL);
}
