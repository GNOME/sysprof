/* gtkcellrendererprogress.h
 * Copyright (C) 2002 Naba Kumar <kh_naba@users.sourceforge.net>
 * modified by Jörgen Scheibengruber <mfcn@gmx.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Modified by the GTK+ Team and others 1997-2004.  See the AUTHORS
 * file for a list of people on the GTK+ Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GTK+ at ftp://ftp.gtk.org/pub/gtk/.
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_CELL_RENDERER_PROGRESS    (sysprof_cell_renderer_progress_get_type ())
#define SYSPROF_CELL_RENDERER_PROGRESS(obj)    (G_TYPE_CHECK_INSTANCE_CAST ((obj), SYSPROF_TYPE_CELL_RENDERER_PROGRESS, SysprofCellRendererProgress))
#define SYSPROF_IS_CELL_RENDERER_PROGRESS(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SYSPROF_TYPE_CELL_RENDERER_PROGRESS))

typedef struct _SysprofCellRendererProgress SysprofCellRendererProgress;
typedef struct _SysprofCellRendererProgressClass  SysprofCellRendererProgressClass;
typedef struct _SysprofCellRendererProgressPrivate SysprofCellRendererProgressPrivate;

struct _SysprofCellRendererProgress
{
  GtkCellRenderer parent_instance;
};

struct _SysprofCellRendererProgressClass
{
  GtkCellRendererClass parent_class;
};

GType            sysprof_cell_renderer_progress_get_type (void) G_GNUC_CONST;
GtkCellRenderer *sysprof_cell_renderer_progress_new      (void);

G_END_DECLS
