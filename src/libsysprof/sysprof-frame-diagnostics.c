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
  SysprofDocument *document;
  GListStore      *diagnostics;
  guint            n_diagnostics;
} FrameDiagnosticState;

static gboolean
add_frame_diagnostic (guint    position,
                      gint64   time,
                      gint64   duration,
                      gpointer user_data)
{
  FrameDiagnosticState *state = user_data;
  g_autoptr(SysprofDocumentMark) mark = NULL;
  g_autoptr(SysprofDiagnostic) diagnostic = NULL;
  g_autofree char *message = NULL;
  SysprofTimeSpan span;

  g_assert (state != NULL);

  /* The frame budget is 8,333,333.33... ns, so the first overrun is
   * 8,333,334 ns. Avoid rounding the budget up to a whole nanosecond.
   */
  if (duration <= SYSPROF_NSEC_PER_SEC / 120 ||
      time > G_MAXINT64 - duration)
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

  state->n_diagnostics++;

  return state->n_diagnostics < SYSPROF_DIAGNOSTIC_PROVIDER_MAX_PER_TYPE;
}

static void
sysprof_frame_diagnostics_populate (SysprofDiagnosticProvider *provider,
                                    SysprofDocument           *document,
                                    GListStore                *diagnostics)
{
  FrameDiagnosticState state = { document, diagnostics, 0 };

  _sysprof_document_foreach_mark (document,
                                  "GTK",
                                  "Frameclock cycle",
                                  add_frame_diagnostic,
                                  &state);
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
