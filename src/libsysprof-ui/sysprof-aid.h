/* sysprof-aid.h
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

#include <gio/gio.h>
#include <sysprof.h>

#include "sysprof-display.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_AID (sysprof_aid_get_type())

G_DECLARE_DERIVABLE_TYPE (SysprofAid, sysprof_aid, SYSPROF, AID, GObject)

struct _SysprofAidClass
{
  GObjectClass parent_class;

  void     (*prepare)        (SysprofAid           *self,
                              SysprofProfiler      *profiler);
  void     (*present_async)  (SysprofAid           *self,
                              SysprofCaptureReader *reader,
                              SysprofDisplay       *display,
                              GCancellable         *cancellable,
                              GAsyncReadyCallback   callback,
                              gpointer              user_data);
  gboolean (*present_finish) (SysprofAid           *self,
                              GAsyncResult         *result,
                              GError              **error);

  /*< private >*/
  gpointer _reserved[16];
};

SysprofAid  *sysprof_aid_new              (const gchar          *display_name,
                                           const gchar          *icon_name);
const gchar *sysprof_aid_get_display_name (SysprofAid           *self);
void         sysprof_aid_set_display_name (SysprofAid           *self,
                                           const gchar          *display_name);
GIcon       *sysprof_aid_get_icon         (SysprofAid           *self);
void         sysprof_aid_set_icon         (SysprofAid           *self,
                                           GIcon                *icon);
void         sysprof_aid_set_icon_name    (SysprofAid           *self,
                                           const gchar          *icon_name);
void         sysprof_aid_prepare          (SysprofAid           *self,
                                           SysprofProfiler      *profiler);
void         sysprof_aid_present_async    (SysprofAid           *self,
                                           SysprofCaptureReader *reader,
                                           SysprofDisplay       *display,
                                           GCancellable         *cancellable,
                                           GAsyncReadyCallback   callback,
                                           gpointer              user_data);
gboolean     sysprof_aid_present_finish   (SysprofAid           *self,
                                           GAsyncResult         *result,
                                           GError              **error);

G_END_DECLS
