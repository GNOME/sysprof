/* sysprof-document-dbus-message.h
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

#include <gio/gio.h>

#include "sysprof-document-frame.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_DOCUMENT_DBUS_MESSAGE         (sysprof_document_dbus_message_get_type())
#define SYSPROF_IS_DOCUMENT_DBUS_MESSAGE(obj)      G_TYPE_CHECK_INSTANCE_TYPE(obj, SYSPROF_TYPE_DOCUMENT_DBUS_MESSAGE)
#define SYSPROF_DOCUMENT_DBUS_MESSAGE(obj)         G_TYPE_CHECK_INSTANCE_CAST(obj, SYSPROF_TYPE_DOCUMENT_DBUS_MESSAGE, SysprofDocumentDBusMessage)
#define SYSPROF_DOCUMENT_DBUS_MESSAGE_CLASS(klass) G_TYPE_CHECK_CLASS_CAST(klass, SYSPROF_TYPE_DOCUMENT_DBUS_MESSAGE, SysprofDocumentDBusMessageClass)

typedef struct _SysprofDocumentDBusMessage      SysprofDocumentDBusMessage;
typedef struct _SysprofDocumentDBusMessageClass SysprofDocumentDBusMessageClass;

SYSPROF_AVAILABLE_IN_ALL
GType              sysprof_document_dbus_message_get_type           (void) G_GNUC_CONST;
SYSPROF_AVAILABLE_IN_ALL
guint              sysprof_document_dbus_message_get_message_length (SysprofDocumentDBusMessage *self);
SYSPROF_AVAILABLE_IN_ALL
const guint8      *sysprof_document_dbus_message_get_message_data   (SysprofDocumentDBusMessage *self,
                                                                     guint                      *length);
SYSPROF_AVAILABLE_IN_ALL
GDBusMessage      *sysprof_document_dbus_message_dup_message        (SysprofDocumentDBusMessage *self);
SYSPROF_AVAILABLE_IN_ALL
guint              sysprof_document_dbus_message_get_serial         (SysprofDocumentDBusMessage *self);
SYSPROF_AVAILABLE_IN_ALL
guint              sysprof_document_dbus_message_get_reply_serial   (SysprofDocumentDBusMessage *self);
SYSPROF_AVAILABLE_IN_ALL
const char        *sysprof_document_dbus_message_get_destination    (SysprofDocumentDBusMessage *self);
SYSPROF_AVAILABLE_IN_ALL
const char        *sysprof_document_dbus_message_get_sender         (SysprofDocumentDBusMessage *self);
SYSPROF_AVAILABLE_IN_ALL
GDBusMessageType   sysprof_document_dbus_message_get_message_type   (SysprofDocumentDBusMessage *self);
SYSPROF_AVAILABLE_IN_ALL
const char        *sysprof_document_dbus_message_get_interface      (SysprofDocumentDBusMessage *self);
SYSPROF_AVAILABLE_IN_ALL
const char        *sysprof_document_dbus_message_get_path           (SysprofDocumentDBusMessage *self);
SYSPROF_AVAILABLE_IN_ALL
const char        *sysprof_document_dbus_message_get_member         (SysprofDocumentDBusMessage *self);
SYSPROF_AVAILABLE_IN_ALL
const char        *sysprof_document_dbus_message_get_signature      (SysprofDocumentDBusMessage *self);
SYSPROF_AVAILABLE_IN_ALL
GDBusMessageFlags  sysprof_document_dbus_message_get_flags          (SysprofDocumentDBusMessage *self);
SYSPROF_AVAILABLE_IN_ALL
GBusType           sysprof_document_dbus_message_get_bus_type       (SysprofDocumentDBusMessage *self);
SYSPROF_AVAILABLE_IN_ALL
char              *sysprof_document_dbus_message_dup_string         (SysprofDocumentDBusMessage *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofDocumentDBusMessage, g_object_unref)

G_END_DECLS
