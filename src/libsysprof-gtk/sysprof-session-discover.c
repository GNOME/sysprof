/* sysprof-session-discover.c
 *
 * Copyright 2023 Christian Hergert <chergert@redhat.com>
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

#include "sysprof-session-private.h"
#include "sysprof-track.h"

void
_sysprof_session_discover_tracks (SysprofSession  *self,
                                  SysprofDocument *document,
                                  GListStore      *tracks)
{
  g_autoptr(SysprofDocumentCounter) cpu = NULL;

  g_assert (SYSPROF_IS_SESSION (self));
  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (G_IS_LIST_STORE (tracks));

  if ((cpu = sysprof_document_find_counter (document, "CPU Percent", "Combined")))
    {
      g_autoptr(SysprofTrack) cpu_track = NULL;

      cpu_track = g_object_new (SYSPROF_TYPE_TRACK,
                                "title", _("CPU Usage"),
                                NULL);
      g_list_store_append (tracks, cpu_track);
    }
}
