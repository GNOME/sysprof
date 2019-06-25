/* sysprof-notebook.h
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

#if !defined (SYSPROF_UI_INSIDE) && !defined (SYSPROF_UI_COMPILATION)
# error "Only <sysprof-ui.h> can be included directly."
#endif

#include <gtk/gtk.h>
#include <sysprof.h>

#include "sysprof-display.h"
#include "sysprof-version-macros.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_NOTEBOOK (sysprof_notebook_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (SysprofNotebook, sysprof_notebook, SYSPROF, NOTEBOOK, GtkNotebook)

struct _SysprofNotebookClass
{
  GtkNotebookClass parent_class;

  /*< private >*/
  gpointer _reserved[16];
};

SYSPROF_AVAILABLE_IN_ALL
GtkWidget      *sysprof_notebook_new                  (void);
SYSPROF_AVAILABLE_IN_ALL
SysprofDisplay *sysprof_notebook_get_current          (SysprofNotebook *self);
SYSPROF_AVAILABLE_IN_ALL
void            sysprof_notebook_close_current        (SysprofNotebook *self);
SYSPROF_AVAILABLE_IN_ALL
void            sysprof_notebook_open                 (SysprofNotebook *self,
                                                       GFile           *file);
SYSPROF_AVAILABLE_IN_ALL
void            sysprof_notebook_save                 (SysprofNotebook *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean        sysprof_notebook_get_can_save         (SysprofNotebook *self);
SYSPROF_AVAILABLE_IN_ALL
void            sysprof_notebook_replay               (SysprofNotebook *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean        sysprof_notebook_get_can_replay       (SysprofNotebook *self);
SYSPROF_AVAILABLE_IN_ALL
void            sysprof_notebook_add_profiler         (SysprofNotebook *self,
                                                       SysprofProfiler *profiler);
SYSPROF_AVAILABLE_IN_ALL
gboolean        sysprof_notebook_get_always_show_tabs (SysprofNotebook *self);
SYSPROF_AVAILABLE_IN_ALL
void            sysprof_notebook_set_always_show_tabs (SysprofNotebook *self,
                                                       gboolean         always_show_tabs);

G_END_DECLS
