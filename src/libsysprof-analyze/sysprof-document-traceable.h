/*
 * sysprof-document-traceable.h
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

#include "sysprof-document-frame.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_DOCUMENT_TRACEABLE (sysprof_document_traceable_get_type ())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (SysprofDocumentTraceable, sysprof_document_traceable, SYSPROF, DOCUMENT_TRACEABLE, SysprofDocumentFrame)

struct _SysprofDocumentTraceableInterface
{
  GTypeInterface parent;

  guint   (*get_stack_depth)     (SysprofDocumentTraceable *self);
  guint64 (*get_stack_address)   (SysprofDocumentTraceable *self,
                                  guint                     position);
  guint   (*get_stack_addresses) (SysprofDocumentTraceable *self,
                                  guint64                  *addresses,
                                  guint                     n_addresses);
  int     (*get_thread_id)       (SysprofDocumentTraceable *self);
};

SYSPROF_AVAILABLE_IN_ALL
guint   sysprof_document_traceable_get_stack_depth     (SysprofDocumentTraceable *self);
SYSPROF_AVAILABLE_IN_ALL
guint64 sysprof_document_traceable_get_stack_address   (SysprofDocumentTraceable *self,
                                                        guint                     position);
SYSPROF_AVAILABLE_IN_ALL
guint   sysprof_document_traceable_get_stack_addresses (SysprofDocumentTraceable *self,
                                                        guint64                  *addresses,
                                                        guint                     n_addresses);
SYSPROF_AVAILABLE_IN_ALL
int     sysprof_document_traceable_get_thread_id       (SysprofDocumentTraceable *self);

G_END_DECLS
