/* sysprof-document-dbus-message.c
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

#include <glib/gi18n.h>

#include "sysprof-document-frame-private.h"
#include "sysprof-document-dbus-message.h"

struct _SysprofDocumentDBusMessage
{
  SysprofDocumentFrame parent_instance;
  GDBusMessage *message;
};

struct _SysprofDocumentDBusMessageClass
{
  SysprofDocumentFrameClass parent_class;
};

enum {
  PROP_0,
  PROP_BUS_TYPE,
  PROP_DESTINATION,
  PROP_FLAGS,
  PROP_INTERFACE,
  PROP_MEMBER,
  PROP_MESSAGE,
  PROP_MESSAGE_LENGTH,
  PROP_MESSAGE_TYPE,
  PROP_PATH,
  PROP_REPLY_SERIAL,
  PROP_SIGNATURE,
  PROP_SENDER,
  PROP_SERIAL,
  PROP_STRING,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofDocumentDBusMessage, sysprof_document_dbus_message, SYSPROF_TYPE_DOCUMENT_FRAME)

static GParamSpec *properties [N_PROPS];

static void
sysprof_document_dbus_message_dispose (GObject *object)
{
  SysprofDocumentDBusMessage *self = (SysprofDocumentDBusMessage *)object;

  g_clear_object (&self->message);

  G_OBJECT_CLASS (sysprof_document_dbus_message_parent_class)->dispose (object);
}

static void
sysprof_document_dbus_message_get_property (GObject    *object,
                                            guint       prop_id,
                                            GValue     *value,
                                            GParamSpec *pspec)
{
  SysprofDocumentDBusMessage *self = SYSPROF_DOCUMENT_DBUS_MESSAGE (object);

  switch (prop_id)
    {
    case PROP_BUS_TYPE:
      g_value_set_enum (value, sysprof_document_dbus_message_get_bus_type (self));
      break;

    case PROP_FLAGS:
      g_value_set_flags (value, sysprof_document_dbus_message_get_flags (self));
      break;

    case PROP_MESSAGE:
      g_value_take_object (value, sysprof_document_dbus_message_dup_message (self));
      break;

    case PROP_MESSAGE_LENGTH:
      g_value_set_uint (value, sysprof_document_dbus_message_get_message_length (self));
      break;

    case PROP_REPLY_SERIAL:
      g_value_set_uint (value, sysprof_document_dbus_message_get_reply_serial (self));
      break;

    case PROP_SERIAL:
      g_value_set_uint (value, sysprof_document_dbus_message_get_serial (self));
      break;

    case PROP_SENDER:
      g_value_set_string (value, sysprof_document_dbus_message_get_sender (self));
      break;

    case PROP_DESTINATION:
      g_value_set_string (value, sysprof_document_dbus_message_get_destination (self));
      break;

    case PROP_MESSAGE_TYPE:
      g_value_set_enum (value, sysprof_document_dbus_message_get_message_type (self));
      break;

    case PROP_INTERFACE:
      g_value_set_string (value, sysprof_document_dbus_message_get_interface (self));
      break;

    case PROP_PATH:
      g_value_set_string (value, sysprof_document_dbus_message_get_path (self));
      break;

    case PROP_MEMBER:
      g_value_set_string (value, sysprof_document_dbus_message_get_member (self));
      break;

    case PROP_SIGNATURE:
      g_value_set_string (value, sysprof_document_dbus_message_get_signature (self));
      break;

    case PROP_STRING:
      g_value_take_string (value, sysprof_document_dbus_message_dup_string (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_document_dbus_message_class_init (SysprofDocumentDBusMessageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofDocumentFrameClass *document_frame_class = SYSPROF_DOCUMENT_FRAME_CLASS (klass);

  object_class->dispose = sysprof_document_dbus_message_dispose;
  object_class->get_property = sysprof_document_dbus_message_get_property;

  document_frame_class->type_name = N_("D-Bus Message");

  properties[PROP_BUS_TYPE] =
    g_param_spec_enum ("bus-type", NULL, NULL,
                       G_TYPE_BUS_TYPE,
                       0,
                       (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_MESSAGE_LENGTH] =
    g_param_spec_uint ("message-length", NULL, NULL,
                       0, G_MAXUINT16, 0,
                       (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_MESSAGE] =
    g_param_spec_object ("message", NULL, NULL,
                         G_TYPE_DBUS_MESSAGE,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_REPLY_SERIAL] =
    g_param_spec_uint ("reply-serial", NULL, NULL,
                       0, G_MAXUINT, 0,
                       (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_SERIAL] =
    g_param_spec_uint ("serial", NULL, NULL,
                       0, G_MAXUINT, 0,
                       (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_SENDER] =
    g_param_spec_string ("sender", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_DESTINATION] =
    g_param_spec_string ("destination", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_INTERFACE] =
    g_param_spec_string ("interface", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_PATH] =
    g_param_spec_string ("path", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_MEMBER] =
    g_param_spec_string ("member", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_SIGNATURE] =
    g_param_spec_string ("signature", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_MESSAGE_TYPE] =
    g_param_spec_enum ("message-type", NULL, NULL,
                       G_TYPE_DBUS_MESSAGE_TYPE,
                       G_DBUS_MESSAGE_TYPE_INVALID,
                       (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_FLAGS] =
    g_param_spec_flags ("flags", NULL, NULL,
                        G_TYPE_DBUS_MESSAGE_FLAGS,
                        0,
                        (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_STRING] =
    g_param_spec_string ("string", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_document_dbus_message_init (SysprofDocumentDBusMessage *self)
{
}

guint
sysprof_document_dbus_message_get_message_length (SysprofDocumentDBusMessage *self)
{
  const SysprofCaptureDBusMessage *dbus_message;
  guint len;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_DBUS_MESSAGE (self), FALSE);

  dbus_message = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureDBusMessage);

  len = SYSPROF_DOCUMENT_FRAME_UINT16 (self, dbus_message->message_len);

  if (len > SYSPROF_DOCUMENT_FRAME (self)->frame_len - sizeof *dbus_message)
    return 0;

  return len;
}

/**
 * sysprof_document_dbus_message_get_message_data:
 * @self: a #SysprofDocumentDBusMessage
 * @length: (out): the location of the length of the data
 *
 * Returns: (transfer none) (nullable): the message data or %NULL
 *   if length is zero.
 */
const guint8 *
sysprof_document_dbus_message_get_message_data (SysprofDocumentDBusMessage *self,
                                                guint                      *length)
{
  const SysprofCaptureDBusMessage *dbus_message;
  guint len;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_DBUS_MESSAGE (self), FALSE);

  dbus_message = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureDBusMessage);
  len = sysprof_document_dbus_message_get_message_length (self);

  if (length != NULL)
    *length = len;

  if (len == 0)
    return NULL;

  return dbus_message->message;
}

/**
 * sysprof_document_dbus_message_dup_message:
 * @self: a #SysprofDocumentDBusMessage
 *
 * Tries to decode the message.
 *
 * Returns: (transfer none) (nullable): a #GDBusMessage or %NULL
 */
GDBusMessage *
sysprof_document_dbus_message_dup_message (SysprofDocumentDBusMessage *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_DBUS_MESSAGE (self), NULL);

  if (self->message == NULL)
    {
      const guint8 *data;
      guint len;

      if ((data = sysprof_document_dbus_message_get_message_data (self, &len)))
        self->message = g_dbus_message_new_from_blob ((guchar *)data, len, G_DBUS_CAPABILITY_FLAGS_UNIX_FD_PASSING, NULL);
    }

  return self->message ? g_object_ref (self->message) : NULL;
}

GDBusMessageFlags
sysprof_document_dbus_message_get_flags (SysprofDocumentDBusMessage *self)
{
  g_autoptr(GDBusMessage) message = NULL;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_DBUS_MESSAGE (self), 0);

  if (!(message = sysprof_document_dbus_message_dup_message (self)))
    return 0;

  return g_dbus_message_get_flags (message);
}

guint
sysprof_document_dbus_message_get_reply_serial (SysprofDocumentDBusMessage *self)
{
  g_autoptr(GDBusMessage) message = NULL;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_DBUS_MESSAGE (self), 0);

  if (!(message = sysprof_document_dbus_message_dup_message (self)))
    return 0;

  return g_dbus_message_get_reply_serial (message);
}

guint
sysprof_document_dbus_message_get_serial (SysprofDocumentDBusMessage *self)
{
  g_autoptr(GDBusMessage) message = NULL;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_DBUS_MESSAGE (self), 0);

  if (!(message = sysprof_document_dbus_message_dup_message (self)))
    return 0;

  return g_dbus_message_get_serial (message);
}

const char *
sysprof_document_dbus_message_get_sender (SysprofDocumentDBusMessage *self)
{
  g_autoptr(GDBusMessage) message = NULL;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_DBUS_MESSAGE (self), NULL);

  if (!(message = sysprof_document_dbus_message_dup_message (self)))
    return NULL;

  /* Safe because of self->message */
  return g_dbus_message_get_sender (message);
}

const char *
sysprof_document_dbus_message_get_destination (SysprofDocumentDBusMessage *self)
{
  g_autoptr(GDBusMessage) message = NULL;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_DBUS_MESSAGE (self), NULL);

  if (!(message = sysprof_document_dbus_message_dup_message (self)))
    return NULL;

  /* Safe because of self->message */
  return g_dbus_message_get_destination (message);
}

const char *
sysprof_document_dbus_message_get_interface (SysprofDocumentDBusMessage *self)
{
  g_autoptr(GDBusMessage) message = NULL;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_DBUS_MESSAGE (self), NULL);

  if (!(message = sysprof_document_dbus_message_dup_message (self)))
    return NULL;

  /* Safe because of self->message */
  return g_dbus_message_get_interface (message);
}

const char *
sysprof_document_dbus_message_get_member (SysprofDocumentDBusMessage *self)
{
  g_autoptr(GDBusMessage) message = NULL;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_DBUS_MESSAGE (self), NULL);

  if (!(message = sysprof_document_dbus_message_dup_message (self)))
    return NULL;

  /* Safe because of self->message */
  return g_dbus_message_get_member (message);
}

const char *
sysprof_document_dbus_message_get_path (SysprofDocumentDBusMessage *self)
{
  g_autoptr(GDBusMessage) message = NULL;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_DBUS_MESSAGE (self), NULL);

  if (!(message = sysprof_document_dbus_message_dup_message (self)))
    return NULL;

  /* Safe because of self->message */
  return g_dbus_message_get_path (message);
}

const char *
sysprof_document_dbus_message_get_signature (SysprofDocumentDBusMessage *self)
{
  g_autoptr(GDBusMessage) message = NULL;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_DBUS_MESSAGE (self), NULL);

  if (!(message = sysprof_document_dbus_message_dup_message (self)))
    return NULL;

  /* Safe because of self->message */
  return g_dbus_message_get_signature (message);
}

GDBusMessageType
sysprof_document_dbus_message_get_message_type (SysprofDocumentDBusMessage *self)
{
  g_autoptr(GDBusMessage) message = NULL;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_DBUS_MESSAGE (self), 0);

  if (!(message = sysprof_document_dbus_message_dup_message (self)))
    return 0;

  return g_dbus_message_get_message_type (message);
}

GBusType
sysprof_document_dbus_message_get_bus_type (SysprofDocumentDBusMessage *self)
{
  const SysprofCaptureDBusMessage *dbus_message;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_DBUS_MESSAGE (self), FALSE);

  dbus_message = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureDBusMessage);

  if (dbus_message->bus_type == 1 || dbus_message->bus_type == 2)
    return dbus_message->bus_type;

  return 0;
}

char *
sysprof_document_dbus_message_dup_string (SysprofDocumentDBusMessage *self)
{
  g_autoptr(GDBusMessage) message = NULL;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_DBUS_MESSAGE (self), NULL);

  if (!(message = sysprof_document_dbus_message_dup_message (self)))
    return NULL;

  return g_dbus_message_print (message, 0);
}
