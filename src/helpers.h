/* helpers.h
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
#ifdef __linux__
# include <linux/perf_event.h>
#endif

G_BEGIN_DECLS

gboolean helpers_list_processes  (gint32      **processes,
                                  gsize        *n_processes);
gboolean helpers_perf_event_open (GVariant     *options,
                                  gint32        pid,
                                  gint32        cpu,
                                  gint          group_fd,
                                  guint64       flags,
                                  gint         *out_fd);
gboolean helpers_get_proc_file   (const gchar  *path,
                                  gchar       **contents,
                                  gsize        *len);

G_END_DECLS
