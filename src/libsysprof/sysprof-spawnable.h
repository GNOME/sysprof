/* sysprof-spawnable.h
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

#include "sysprof-version-macros.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_SPAWNABLE (sysprof_spawnable_get_type())

typedef void (*SysprofSpawnableFDForeach) (gint     dest_fd,
                                           gint     fd,
                                           gpointer user_data);

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SysprofSpawnable, sysprof_spawnable, SYSPROF, SPAWNABLE, GObject)

SYSPROF_AVAILABLE_IN_ALL
SysprofSpawnable    *sysprof_spawnable_new             (void);
SYSPROF_AVAILABLE_IN_ALL
void                 sysprof_spawnable_prepend_argv    (SysprofSpawnable          *self,
                                                        const gchar               *argv);
SYSPROF_AVAILABLE_IN_ALL
void                 sysprof_spawnable_append_argv     (SysprofSpawnable          *self,
                                                        const gchar               *argv);
SYSPROF_AVAILABLE_IN_ALL
void                 sysprof_spawnable_append_args     (SysprofSpawnable          *self,
                                                        const gchar * const       *argv);
SYSPROF_AVAILABLE_IN_ALL
void                 sysprof_spawnable_set_cwd         (SysprofSpawnable          *self,
                                                        const gchar               *cwd);
SYSPROF_AVAILABLE_IN_ALL
const gchar * const *sysprof_spawnable_get_argv        (SysprofSpawnable          *self);
SYSPROF_AVAILABLE_IN_ALL
const gchar * const *sysprof_spawnable_get_environ     (SysprofSpawnable          *self);
SYSPROF_AVAILABLE_IN_ALL
void                 sysprof_spawnable_set_environ     (SysprofSpawnable          *self,
                                                        const gchar * const       *environ);
SYSPROF_AVAILABLE_IN_ALL
void                 sysprof_spawnable_setenv          (SysprofSpawnable          *self,
                                                        const gchar               *key,
                                                        const gchar               *value);
SYSPROF_AVAILABLE_IN_ALL
const gchar         *sysprof_spawnable_getenv          (SysprofSpawnable          *self,
                                                        const gchar               *key);
SYSPROF_AVAILABLE_IN_ALL
gint                 sysprof_spawnable_take_fd         (SysprofSpawnable          *self,
                                                        gint                       fd,
                                                        gint                       dest_fd);
SYSPROF_AVAILABLE_IN_ALL
void                 sysprof_spawnable_foreach_fd      (SysprofSpawnable          *self,
                                                        SysprofSpawnableFDForeach  foreach,
                                                        gpointer                   user_data);
SYSPROF_AVAILABLE_IN_ALL
void                 sysprof_spawnable_set_starting_fd (SysprofSpawnable          *self,
                                                        gint                       starting_fd);
SYSPROF_AVAILABLE_IN_ALL
GSubprocess         *sysprof_spawnable_spawn           (SysprofSpawnable          *self,
                                                        GError                   **error);

G_END_DECLS
