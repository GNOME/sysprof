/*
 * sysprof-document-traceable.c
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

#include "config.h"

#include "sysprof-document-traceable.h"

G_DEFINE_INTERFACE (SysprofDocumentTraceable, sysprof_document_traceable, SYSPROF_TYPE_DOCUMENT_FRAME)

static void
sysprof_document_traceable_default_init (SysprofDocumentTraceableInterface *iface)
{
  /**
   * SysprofDocumentTraceable:stack-depth:
   *
   * The "stack-depth" property contains the number of addresses collected
   * in the backtrace.
   *
   * You may use this value to retrieve the addresses from 0 to ("stack-depth"-1)
   * by calling sysprof_document_traceable_get_stack_address().
   *
   * Since: 45
   */
  g_object_interface_install_property (iface,
                                       g_param_spec_uint ("stack-depth", NULL, NULL,
                                                          0, G_MAXUINT16, 0,
                                                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
}

guint
sysprof_document_traceable_get_stack_depth (SysprofDocumentTraceable *self)
{
  return SYSPROF_DOCUMENT_TRACEABLE_GET_IFACE (self)->get_stack_depth (self);
}

guint64
sysprof_document_traceable_get_stack_address (SysprofDocumentTraceable *self,
                                              guint                     position)
{
  return SYSPROF_DOCUMENT_TRACEABLE_GET_IFACE (self)->get_stack_address (self, position);
}
