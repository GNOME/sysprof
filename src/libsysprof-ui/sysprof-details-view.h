/* sysprof-details-view.h
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

#include <gtk/gtk.h>
#include <sysprof-capture.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_DETAILS_VIEW (sysprof_details_view_get_type())

G_DECLARE_FINAL_TYPE (SysprofDetailsView, sysprof_details_view, SYSPROF, DETAILS_VIEW, GtkBin)

GtkWidget *sysprof_details_view_new        (void);
void       sysprof_details_view_set_reader (SysprofDetailsView   *self,
                                            SysprofCaptureReader *reader);
void       sysprof_details_view_add_mark   (SysprofDetailsView   *self,
                                            const gchar          *mark,
                                            gint64                min,
                                            gint64                max,
                                            gint64                avg,
                                            gint64                hits);
void       sysprof_details_view_add_item   (SysprofDetailsView   *self,
                                            GtkWidget            *left,
                                            GtkWidget            *center);

G_END_DECLS
