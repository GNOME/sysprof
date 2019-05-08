/* sysprof-profile.c
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

#include "config.h"

#include "sysprof-profile.h"

G_DEFINE_INTERFACE (SysprofProfile, sysprof_profile, G_TYPE_OBJECT)

static void
dummy_generate (SysprofProfile           *self,
                GCancellable        *cancellable,
                GAsyncReadyCallback  callback,
                gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_return_boolean (task, TRUE);
}

static gboolean
dummy_generate_finish (SysprofProfile     *self,
                       GAsyncResult  *result,
                       GError       **error)
{
  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
sysprof_profile_default_init (SysprofProfileInterface *iface)
{
  iface->generate = dummy_generate;
  iface->generate_finish = dummy_generate_finish;
}

void
sysprof_profile_generate (SysprofProfile           *self,
                     GCancellable        *cancellable,
                     GAsyncReadyCallback  callback,
                     gpointer             user_data)
{
  g_return_if_fail (SYSPROF_IS_PROFILE (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  SYSPROF_PROFILE_GET_IFACE (self)->generate (self, cancellable, callback, user_data);
}

gboolean
sysprof_profile_generate_finish (SysprofProfile     *self,
                            GAsyncResult  *result,
                            GError       **error)
{
  g_return_val_if_fail (SYSPROF_IS_PROFILE (self), FALSE);
  g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

  return SYSPROF_PROFILE_GET_IFACE (self)->generate_finish (self, result, error);
}

void
sysprof_profile_set_reader (SysprofProfile       *self,
                       SysprofCaptureReader *reader)
{
  g_return_if_fail (SYSPROF_IS_PROFILE (self));
  g_return_if_fail (reader != NULL);

  SYSPROF_PROFILE_GET_IFACE (self)->set_reader (self, reader);
}
