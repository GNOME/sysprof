/* sysprof-line-reader.h
 *
 * Copyright 2015-2019 Christian Hergert <christian@hergert.me>
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

#include <glib.h>

G_BEGIN_DECLS

typedef struct _SysprofLineReader SysprofLineReader;

G_GNUC_INTERNAL
SysprofLineReader *sysprof_line_reader_new  (const gchar       *contents,
                                             gssize             length);
G_GNUC_INTERNAL
void               sysprof_line_reader_free (SysprofLineReader *self);
G_GNUC_INTERNAL
const gchar       *sysprof_line_reader_next (SysprofLineReader *self,
                                             gsize             *length);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofLineReader, sysprof_line_reader_free)

G_END_DECLS
