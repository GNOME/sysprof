/* sysprof-display.h
 *
 * Copyright 2019 Christian Hergert <chergert@redhat.com>
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

#pragma once

#include <gtk/gtk.h>
#include <sysprof.h>

#include "sysprof-version-macros.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_DISPLAY (sysprof_display_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (SysprofDisplay, sysprof_display, SYSPROF, DISPLAY, GtkBin)

struct _SysprofDisplayClass
{
  GtkBinClass parent_class;

  /*< private >*/
  gpointer _reserved[16];
} __attribute__((aligned(8)));

SYSPROF_AVAILABLE_IN_ALL
GtkWidget       *sysprof_display_new            (void);
SYSPROF_AVAILABLE_IN_ALL
gchar           *sysprof_display_dup_title      (SysprofDisplay *self);
SYSPROF_AVAILABLE_IN_ALL
SysprofProfiler *sysprof_display_get_profiler   (SysprofDisplay *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean         sysprof_display_is_empty       (SysprofDisplay *self);
SYSPROF_AVAILABLE_IN_ALL
void             sysprof_display_open           (SysprofDisplay *self,
                                                 GFile          *file);
SYSPROF_AVAILABLE_IN_ALL
void             sysprof_display_save           (SysprofDisplay *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean         sysprof_display_get_can_save   (SysprofDisplay *self);
SYSPROF_AVAILABLE_IN_ALL
void             sysprof_display_stop_recording (SysprofDisplay *self);

G_END_DECLS
