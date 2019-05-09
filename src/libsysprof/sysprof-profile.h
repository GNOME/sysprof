/* sysprof-profile.h
 *
 * Copyright 2016-2019 Christian Hergert <christian@hergert.me>
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

#if !defined (SYSPROF_INSIDE) && !defined (SYSPROF_COMPILATION)
# error "Only <sysprof.h> can be included directly."
#endif

#include <gio/gio.h>

#include "sysprof-capture-reader.h"
#include "sysprof-version-macros.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_PROFILE (sysprof_profile_get_type ())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (SysprofProfile, sysprof_profile, SYSPROF, PROFILE, GObject)

struct _SysprofProfileInterface
{
  GTypeInterface parent;

  void     (*set_reader)      (SysprofProfile            *self,
                               SysprofCaptureReader      *reader);
  void     (*generate)        (SysprofProfile            *self,
                               GCancellable         *cancellable,
                               GAsyncReadyCallback   callback,
                               gpointer              user_data);
  gboolean (*generate_finish) (SysprofProfile            *self,
                               GAsyncResult         *result,
                               GError              **error);
};

SYSPROF_AVAILABLE_IN_ALL
void     sysprof_profile_set_reader      (SysprofProfile        *self,
                                          SysprofCaptureReader  *reader);
SYSPROF_AVAILABLE_IN_ALL
void     sysprof_profile_generate        (SysprofProfile        *self,
                                          GCancellable          *cancellable,
                                          GAsyncReadyCallback    callback,
                                          gpointer               user_data);
SYSPROF_AVAILABLE_IN_ALL
gboolean sysprof_profile_generate_finish (SysprofProfile        *self,
                                          GAsyncResult          *result,
                                          GError               **error);

G_END_DECLS
