/* sysprof-session-model.h
 *
 * Copyright 2023 Christian Hergert <chergert@redhat.com>
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

#include <sysprof.h>

#include "sysprof-session.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_SESSION_MODEL (sysprof_session_model_get_type())

G_DECLARE_FINAL_TYPE (SysprofSessionModel, sysprof_session_model, SYSPROF, SESSION_MODEL, GObject)

SysprofSessionModel *sysprof_session_model_new         (SysprofSession      *session,
                                                        GListModel          *model);
GListModel          *sysprof_session_model_get_model   (SysprofSessionModel *self);
void                 sysprof_session_model_set_model   (SysprofSessionModel *self,
                                                        GListModel          *model);
SysprofSession      *sysprof_session_model_get_session (SysprofSessionModel *self);
void                 sysprof_session_model_set_session (SysprofSessionModel *self,
                                                        SysprofSession      *session);

G_END_DECLS
