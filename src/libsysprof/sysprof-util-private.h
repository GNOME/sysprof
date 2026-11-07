/*
 * sysprof-util-private.h
 *
 * Copyright 2025 Christian Hergert <chergert@redhat.com>
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

#include <libdex.h>

G_BEGIN_DECLS

GArray    *_sysprof_get_online_cpus    (GError          **error);
DexFuture *_sysprof_list_directory     (const char       *path) G_GNUC_WARN_UNUSED_RESULT;
DexFuture *_sysprof_list_online_cpus   (void) G_GNUC_WARN_UNUSED_RESULT;
DexFuture *_sysprof_load_file          (const char       *path) G_GNUC_WARN_UNUSED_RESULT;
GArray    *_sysprof_parse_cpu_list     (const char       *cpu_list,
                                        GError          **error);
DexFuture *sysprof_get_proc_file_bytes (GDBusConnection  *connection,
                                        const char       *path);

G_END_DECLS
