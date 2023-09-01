/* sysprof-document-process-private.h
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

#include "sysprof-document-process.h"
#include "sysprof-process-info-private.h"

G_BEGIN_DECLS

SysprofProcessInfo *_sysprof_document_process_get_info (SysprofDocumentProcess *self);
void                _sysprof_document_process_set_info (SysprofDocumentProcess *self,
                                                        SysprofProcessInfo     *process_info);
const char         *_sysprof_document_process_get_comm (SysprofDocumentProcess *self);

G_END_DECLS
