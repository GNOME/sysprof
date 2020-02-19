/* sysprof-page.h
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

G_BEGIN_DECLS

#define SYSPROF_TYPE_PAGE (sysprof_page_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (SysprofPage, sysprof_page, SYSPROF, PAGE, GtkBin)

struct _SysprofPageClass
{
  GtkBinClass parent_class;

  void     (*load_async)      (SysprofPage              *self,
                               SysprofCaptureReader     *reader,
                               SysprofSelection         *selection,
                               SysprofCaptureCondition  *condition,
                               GCancellable             *cancellable,
                               GAsyncReadyCallback       callback,
                               gpointer                  user_data);
  gboolean (*load_finish)     (SysprofPage              *self,
                               GAsyncResult             *result,
                               GError                  **error);
  void     (*set_hadjustment) (SysprofPage              *self,
                               GtkAdjustment            *hadjustment);
  void     (*set_size_group)  (SysprofPage              *self,
                               GtkSizeGroup             *size_group);

  /*< private >*/
  gpointer _reserved[16];
};

SYSPROF_AVAILABLE_IN_ALL
GtkWidget   *sysprof_page_new             (void);
SYSPROF_AVAILABLE_IN_ALL
void         sysprof_page_load_async      (SysprofPage              *self,
                                           SysprofCaptureReader     *reader,
                                           SysprofSelection         *selection,
                                           SysprofCaptureCondition  *condition,
                                           GCancellable             *cancellable,
                                           GAsyncReadyCallback       callback,
                                           gpointer                  user_data);
SYSPROF_AVAILABLE_IN_ALL
gboolean     sysprof_page_load_finish     (SysprofPage              *self,
                                           GAsyncResult             *result,
                                           GError                  **error);
SYSPROF_AVAILABLE_IN_3_36
void         sysprof_page_reload          (SysprofPage              *self);
SYSPROF_AVAILABLE_IN_ALL
const gchar *sysprof_page_get_title       (SysprofPage              *self);
SYSPROF_AVAILABLE_IN_ALL
void         sysprof_page_set_title       (SysprofPage              *self,
                                           const gchar              *title);
SYSPROF_AVAILABLE_IN_ALL
void         sysprof_page_set_hadjustment (SysprofPage              *self,
                                           GtkAdjustment            *hadjustment);
SYSPROF_AVAILABLE_IN_ALL
void         sysprof_page_set_size_group  (SysprofPage              *self,
                                           GtkSizeGroup             *size_group);

G_END_DECLS
