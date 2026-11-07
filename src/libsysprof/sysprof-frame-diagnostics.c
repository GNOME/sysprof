/* sysprof-frame-diagnostics.c
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

#include <glib/gi18n.h>

#include "sysprof-diagnostic-private.h"
#include "sysprof-frame-cycle.h"
#include "sysprof-frame-diagnostics-private.h"

struct _SysprofFrameDiagnostics
{
  SysprofDiagnosticProvider parent_instance;
};

G_DEFINE_FINAL_TYPE (SysprofFrameDiagnostics, sysprof_frame_diagnostics, SYSPROF_TYPE_DIAGNOSTIC_PROVIDER)

static void
sysprof_frame_diagnostics_populate (SysprofDiagnosticProvider *provider,
                                    SysprofDocument           *document,
                                    GListStore                *diagnostics)
{
  g_autoptr(SysprofFrameCycle) cycle = NULL;
  guint emitted = 0;

  cycle = sysprof_frame_cycle_new (document);

  for (guint i = 0; i < sysprof_frame_cycle_get_n_frames (cycle); i++)
    {
      const SysprofFrameCycleFrame *frame = sysprof_frame_cycle_get_frame (cycle, i);
      g_autoptr(SysprofDiagnostic) diagnostic = NULL;
      g_autofree char *message = NULL;
      const char *output;
      SysprofTimeSpan span;

      if (emitted >= SYSPROF_DIAGNOSTIC_PROVIDER_MAX_PER_TYPE)
        break;

      if (frame->kind != SYSPROF_FRAME_CYCLE_COMPOSITOR ||
          frame->result != SYSPROF_FRAME_CYCLE_PRESENTED ||
          frame->target_presentation <= 0 ||
          frame->presentation <= frame->target_presentation ||
          frame->correlation_estimated ||
          frame->presentation_estimated ||
          frame->dispatch_mark == NULL ||
          frame->presentation_mark == NULL)
        continue;

      output = sysprof_frame_cycle_get_lane (cycle, frame->lane);
      message = g_strdup_printf (_("Frame on %s presented %.3f ms after its target"),
                                 output,
                                 (frame->presentation - frame->target_presentation) / 1000000.);

      span.begin_nsec = MIN (frame->begin, frame->target_presentation);
      span.end_nsec = MAX (frame->presentation, frame->dispatch_end);

      diagnostic = g_object_new (SYSPROF_TYPE_DIAGNOSTIC, NULL);
      _sysprof_diagnostic_set_details (diagnostic, "Mutter", "frames", message, &span);
      _sysprof_diagnostic_add_frame (diagnostic, SYSPROF_DOCUMENT_FRAME (frame->dispatch_mark));
      _sysprof_diagnostic_add_frame (diagnostic, SYSPROF_DOCUMENT_FRAME (frame->presentation_mark));

      g_list_store_append (diagnostics, diagnostic);
      emitted++;
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
