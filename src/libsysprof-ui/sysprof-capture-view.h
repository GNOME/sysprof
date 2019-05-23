/* sysprof-capture-view.h
 *
 * Copyright 2019 Christian Hergert <unknown@domain.org>
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
#include <sysprof-capture.h>

#include "sysprof-zoom-manager.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_CAPTURE_VIEW (sysprof_capture_view_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (SysprofCaptureView, sysprof_capture_view, SYSPROF, CAPTURE_VIEW, GtkBin)

struct _SysprofCaptureViewClass
{
  GtkBinClass parent_class;
  
  void     (*load_async)  (SysprofCaptureView   *self,
                           SysprofCaptureReader *reader,
                           GCancellable         *cancellable,
                           GAsyncReadyCallback   callback,
                           gpointer              user_data);
  gboolean (*load_finish) (SysprofCaptureView   *self,
                           GAsyncResult         *result,
                           GError              **error);

  /*< private >*/
  gpointer _reserved[32];
};

SYSPROF_AVAILABLE_IN_ALL
GtkWidget            *sysprof_capture_view_new              (void);
SYSPROF_AVAILABLE_IN_ALL
SysprofZoomManager   *sysprof_capture_view_get_zoom_manager (SysprofCaptureView *self);
SYSPROF_AVAILABLE_IN_ALL
SysprofCaptureReader *sysprof_capture_view_get_reader       (SysprofCaptureView    *self);
SYSPROF_AVAILABLE_IN_ALL
void                  sysprof_capture_view_load_async       (SysprofCaptureView    *self,
                                                             SysprofCaptureReader  *reader,
                                                             GCancellable          *cancellable,
                                                             GAsyncReadyCallback    callback,
                                                             gpointer               user_data);
SYSPROF_AVAILABLE_IN_ALL
gboolean              sysprof_capture_view_load_finish      (SysprofCaptureView    *self,
                                                             GAsyncResult          *result,
                                                             GError               **error);
SYSPROF_AVAILABLE_IN_ALL
gboolean              sysprof_capture_view_get_busy         (SysprofCaptureView    *self);
SYSPROF_AVAILABLE_IN_ALL
void                  sysprof_capture_view_fit_to_width     (SysprofCaptureView    *self);
SYSPROF_AVAILABLE_IN_ALL
gboolean              sysprof_capture_view_get_can_replay   (SysprofCaptureView    *self);

G_END_DECLS
