/* sysprof-helpers.h
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

#define SYSPROF_TYPE_HELPERS (sysprof_helpers_get_type())

G_DECLARE_FINAL_TYPE (SysprofHelpers, sysprof_helpers, SYSPROF, HELPERS, GObject)

SysprofHelpers *sysprof_helpers_get_default            (void);
void            sysprof_helpers_authorize_async        (SysprofHelpers          *self,
                                                        GCancellable            *cancellable,
                                                        GAsyncReadyCallback      callback,
                                                        gpointer                 user_data);
gboolean        sysprof_helpers_authorize_finish       (SysprofHelpers          *self,
                                                        GAsyncResult            *result,
                                                        GError                 **error);
gboolean        sysprof_helpers_list_processes         (SysprofHelpers          *self,
                                                        GCancellable            *cancellable,
                                                        gint32                 **processes,
                                                        gsize                   *n_processes,
                                                        GError                 **error);
void            sysprof_helpers_list_processes_async   (SysprofHelpers          *self,
                                                        GCancellable            *cancellable,
                                                        GAsyncReadyCallback      callback,
                                                        gpointer                 user_data);
gboolean        sysprof_helpers_list_processes_finish  (SysprofHelpers          *self,
                                                        GAsyncResult            *result,
                                                        gint32                 **processes,
                                                        gsize                   *n_processes,
                                                        GError                 **error);
gboolean        sysprof_helpers_get_proc_fd            (SysprofHelpers          *self,
                                                        const gchar             *path,
                                                        GCancellable            *cancellable,
                                                        gint                    *out_fd,
                                                        GError                 **error);
gboolean        sysprof_helpers_get_proc_file          (SysprofHelpers          *self,
                                                        const gchar             *path,
                                                        GCancellable            *cancellable,
                                                        gchar                  **contents,
                                                        GError                 **error);
void            sysprof_helpers_get_proc_file_async    (SysprofHelpers          *self,
                                                        const gchar             *path,
                                                        GCancellable            *cancellable,
                                                        GAsyncReadyCallback      callback,
                                                        gpointer                 user_data);
gboolean        sysprof_helpers_get_proc_file_finish   (SysprofHelpers          *self,
                                                        GAsyncResult            *result,
                                                        gchar                  **contents,
                                                        GError                 **error);
gboolean        sysprof_helpers_get_process_info       (SysprofHelpers          *self,
                                                        const gchar             *attributes,
                                                        gboolean                 no_proxy,
                                                        GCancellable            *cancellable,
                                                        GVariant               **info,
                                                        GError                 **error);
void            sysprof_helpers_get_process_info_async (SysprofHelpers          *self,
                                                        const gchar             *attributes,
                                                        GCancellable            *cancellable,
                                                        GAsyncReadyCallback      callback,
                                                        gpointer                 user_data);
gboolean        sysprof_helpers_get_process_info_finish(SysprofHelpers          *self,
                                                        GAsyncResult            *result,
                                                        GVariant               **info,
                                                        GError                 **error);
#ifdef __linux__
gboolean        sysprof_helpers_perf_event_open        (SysprofHelpers          *self,
                                                        struct perf_event_attr  *attr,
                                                        gint32                   pid,
                                                        gint32                   cpu,
                                                        gint32                   group_fd,
                                                        guint64                  flags,
                                                        GCancellable            *cancellable,
                                                        gint                    *out_fd,
                                                        GError                 **error);
#endif
void            sysprof_helpers_set_governor_async     (SysprofHelpers          *self,
                                                        const gchar             *governor,
                                                        GCancellable            *cancellable,
                                                        GAsyncReadyCallback      callback,
                                                        gpointer                 user_data);
gboolean        sysprof_helpers_set_governor_finish    (SysprofHelpers          *self,
                                                        GAsyncResult            *result,
                                                        gchar                  **old_governor,
                                                        GError                 **error);
void            sysprof_helpers_set_paranoid_async     (SysprofHelpers          *self,
                                                        int                      paranoid,
                                                        GCancellable            *cancellable,
                                                        GAsyncReadyCallback      callback,
                                                        gpointer                 user_data);
gboolean        sysprof_helpers_set_paranoid_finish    (SysprofHelpers          *self,
                                                        GAsyncResult            *result,
                                                        int                     *old_paranoid,
                                                        GError                 **error);

G_END_DECLS
