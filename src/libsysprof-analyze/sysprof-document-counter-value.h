/* sysprof-document-counter-value.h
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

#include "sysprof-document-counter.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_DOCUMENT_COUNTER_VALUE (sysprof_document_counter_value_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SysprofDocumentCounterValue, sysprof_document_counter_value, SYSPROF, DOCUMENT_COUNTER_VALUE, GObject)

SYSPROF_AVAILABLE_IN_ALL
SysprofDocumentCounter *sysprof_document_counter_value_get_counter      (SysprofDocumentCounterValue *self);
SYSPROF_AVAILABLE_IN_ALL
gint64                  sysprof_document_counter_value_get_time         (SysprofDocumentCounterValue *self);
SYSPROF_AVAILABLE_IN_ALL
gint64                  sysprof_document_counter_value_get_time_offset  (SysprofDocumentCounterValue *self);
SYSPROF_AVAILABLE_IN_ALL
void                    sysprof_document_counter_value_get_value        (SysprofDocumentCounterValue *self,
                                                                         GValue                      *value);
SYSPROF_AVAILABLE_IN_ALL
gint64                  sysprof_document_counter_value_get_value_int64  (SysprofDocumentCounterValue *self);
SYSPROF_AVAILABLE_IN_ALL
double                  sysprof_document_counter_value_get_value_double (SysprofDocumentCounterValue *self);
SYSPROF_AVAILABLE_IN_ALL
char                   *sysprof_document_counter_value_format           (SysprofDocumentCounterValue *self);


G_END_DECLS
