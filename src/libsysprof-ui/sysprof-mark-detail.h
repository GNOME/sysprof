/* sysprof-mark-detail.h
 *
 * Copyright 2022 Corentin Noël <corentin.noel@collabora.com>
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

G_BEGIN_DECLS

#define SYSPROF_TYPE_MARK_DETAIL (sysprof_mark_detail_get_type())

G_DECLARE_FINAL_TYPE (SysprofMarkDetail, sysprof_mark_detail, SYSPROF, MARK_DETAIL, GObject)

SysprofMarkDetail *sysprof_mark_detail_new (const gchar *mark,
                                            gint64       min,
                                            gint64       max,
                                            gint64       avg,
                                            gint64       hits);

G_END_DECLS
