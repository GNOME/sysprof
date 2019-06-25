/* sysprof-marks-page.h
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

#include "sysprof-marks-model.h"
#include "sysprof-page.h"
#include "sysprof-zoom-manager.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_MARKS_PAGE (sysprof_marks_page_get_type())

G_DECLARE_DERIVABLE_TYPE (SysprofMarksPage, sysprof_marks_page, SYSPROF, MARKS_PAGE, SysprofPage)

struct _SysprofMarksPageClass
{
  SysprofPageClass parent_class;

  /*< private >*/
  gpointer _reserved[16];
};

GtkWidget *sysprof_marks_page_new (SysprofZoomManager    *zoom_manager,
                                   SysprofMarksModelKind  kind);

G_END_DECLS
