/* sp-profile.h
 *
 * Copyright (C) 2016 Christian Hergert <christian@hergert.me>
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
 */

#ifndef SP_PROFILE_H
#define SP_PROFILE_H

#include <gio/gio.h>

#include "capture/sp-capture-reader.h"

G_BEGIN_DECLS

#define SP_TYPE_PROFILE (sp_profile_get_type ())

G_DECLARE_INTERFACE (SpProfile, sp_profile, SP, PROFILE, GObject)

struct _SpProfileInterface
{
  GTypeInterface parent;

  void     (*set_reader)      (SpProfile            *self,
                               SpCaptureReader      *reader);
  void     (*generate)        (SpProfile            *self,
                               GCancellable         *cancellable,
                               GAsyncReadyCallback   callback,
                               gpointer              user_data);
  gboolean (*generate_finish) (SpProfile            *self,
                               GAsyncResult         *result,
                               GError              **error);
};

void     sp_profile_set_reader      (SpProfile            *self,
                                     SpCaptureReader      *reader);
void     sp_profile_generate        (SpProfile            *self,
                                     GCancellable         *cancellable,
                                     GAsyncReadyCallback   callback,
                                     gpointer              user_data);
gboolean sp_profile_generate_finish (SpProfile            *self,
                                     GAsyncResult         *result,
                                     GError              **error);

G_END_DECLS

#endif /* SP_PROFILE_H */
