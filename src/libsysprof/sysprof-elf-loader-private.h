/*
 * sysprof-elf-loader-private.h
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

#include <glib-object.h>

#include "sysprof-elf-private.h"
#include "sysprof-mount-namespace-private.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_ELF_LOADER (sysprof_elf_loader_get_type())

G_DECLARE_FINAL_TYPE (SysprofElfLoader, sysprof_elf_loader, SYSPROF, ELF_LOADER, GObject)

SysprofElfLoader   *sysprof_elf_loader_new                     (void);
const char * const *sysprof_elf_loader_get_debug_dirs          (SysprofElfLoader       *self);
void                sysprof_elf_loader_set_debug_dirs          (SysprofElfLoader       *self,
                                                                const char * const     *debug_dirs);
const char * const *sysprof_elf_loader_get_external_debug_dirs (SysprofElfLoader       *self);
void                sysprof_elf_loader_set_external_debug_dirs (SysprofElfLoader       *self,
                                                                const char * const     *debug_dirs);
SysprofElf         *sysprof_elf_loader_load                    (SysprofElfLoader       *self,
                                                                SysprofMountNamespace  *mount_namespace,
                                                                const char             *file,
                                                                const char             *build_id,
                                                                guint64                 file_inode,
                                                                GError                **error);


G_END_DECLS
