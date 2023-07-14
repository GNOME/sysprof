/* sysprof-document-counter.h
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

#include <sysprof-capture.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_DOCUMENT_COUNTER (sysprof_document_counter_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SysprofDocumentCounter, sysprof_document_counter, SYSPROF, DOCUMENT_COUNTER, GObject)

SYSPROF_AVAILABLE_IN_ALL
char       *sysprof_document_counter_dup_key          (SysprofDocumentCounter *self);
SYSPROF_AVAILABLE_IN_ALL
const char *sysprof_document_counter_get_category     (SysprofDocumentCounter *self);
SYSPROF_AVAILABLE_IN_ALL
const char *sysprof_document_counter_get_description  (SysprofDocumentCounter *self);
SYSPROF_AVAILABLE_IN_ALL
const char *sysprof_document_counter_get_name         (SysprofDocumentCounter *self);
SYSPROF_AVAILABLE_IN_ALL
guint       sysprof_document_counter_get_id           (SysprofDocumentCounter *self);
SYSPROF_AVAILABLE_IN_ALL
GType       sysprof_document_counter_get_value_type   (SysprofDocumentCounter *self);
SYSPROF_AVAILABLE_IN_ALL
guint       sysprof_document_counter_get_n_values     (SysprofDocumentCounter *self);
SYSPROF_AVAILABLE_IN_ALL
void        sysprof_document_counter_get_value        (SysprofDocumentCounter *self,
                                                       guint                   nth,
                                                       gint64                 *time,
                                                       GValue                 *value);
SYSPROF_AVAILABLE_IN_ALL
gint64      sysprof_document_counter_get_value_int64  (SysprofDocumentCounter *self,
                                                       guint                   nth,
                                                       gint64                 *time);
SYSPROF_AVAILABLE_IN_ALL
double      sysprof_document_counter_get_value_double (SysprofDocumentCounter *self,
                                                       guint                   nth,
                                                       gint64                 *time);
SYSPROF_AVAILABLE_IN_ALL
double      sysprof_document_counter_get_max_value    (SysprofDocumentCounter *self);
SYSPROF_AVAILABLE_IN_ALL
double      sysprof_document_counter_get_min_value    (SysprofDocumentCounter *self);

G_END_DECLS
