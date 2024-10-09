/* sysprof-elf-private.h
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

G_BEGIN_DECLS

#define SYSPROF_TYPE_ELF (sysprof_elf_get_type())

G_DECLARE_FINAL_TYPE (SysprofElf, sysprof_elf, SYSPROF, ELF, GObject)

SysprofElf *sysprof_elf_new                   (const char   *filename,
                                               GMappedFile  *mapped_file,
                                               guint64       file_inode,
                                               GError      **error);
gboolean    sysprof_elf_matches               (SysprofElf   *self,
                                               guint64       file_inode,
                                               const char   *build_id);
const char *sysprof_elf_get_nick              (SysprofElf   *self);
const char *sysprof_elf_get_file              (SysprofElf   *self);
const char *sysprof_elf_get_build_id          (SysprofElf   *self);
const char *sysprof_elf_get_debug_link        (SysprofElf   *self);
char       *sysprof_elf_get_symbol_at_address (SysprofElf   *self,
                                               guint64       address,
                                               guint64      *begin_address,
                                               guint64      *end_address);
SysprofElf *sysprof_elf_get_debug_link_elf    (SysprofElf   *self);
void        sysprof_elf_set_debug_link_elf    (SysprofElf   *self,
                                               SysprofElf   *debug_link_elf);

G_END_DECLS
