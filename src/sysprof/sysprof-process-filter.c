/* sysprof-process-filter.c
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

#include "sysprof-process-filter-private.h"

static gboolean
process_matches (gpointer item,
                 gpointer user_data)
{
  SysprofDocumentProcess *process = item;
  const SysprofTimeSpan *time_span = user_data;

  g_assert (SYSPROF_IS_DOCUMENT_PROCESS (process));

  if (time_span == NULL)
    return TRUE;

  /* The initial process snapshot predates the recording's start time.
   * Include any process alive during the selection, regardless of when
   * its process record was written.
   */
  return time_span->begin_nsec <= time_span->end_nsec &&
         sysprof_document_frame_get_time (SYSPROF_DOCUMENT_FRAME (process)) <= time_span->end_nsec &&
         sysprof_document_process_get_exit_time (process) >= time_span->begin_nsec;
}

GtkFilter *
_sysprof_process_filter_new (const SysprofTimeSpan *time_span)
{
  return GTK_FILTER (gtk_custom_filter_new (process_matches,
                                            time_span ? g_memdup2 (time_span, sizeof *time_span) : NULL,
                                            g_free));
}
