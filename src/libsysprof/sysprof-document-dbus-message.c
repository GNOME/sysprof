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
  PROP_MESSAGE,
  PROP_MESSAGE_LENGTH,
  PROP_SERIAL,
  PROP_SENDER,
  PROP_DESTINATION,
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
    case PROP_MESSAGE:
      g_value_take_object (value, sysprof_document_dbus_message_dup_message (self));
      break;

    case PROP_MESSAGE_LENGTH:
      g_value_set_uint (value, sysprof_document_dbus_message_get_message_length (self));
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

  properties[PROP_MESSAGE_LENGTH] =
    g_param_spec_uint ("message-length", NULL, NULL,
                       0, G_MAXUINT16, 0,
                       (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_MESSAGE] =
    g_param_spec_object ("message", NULL, NULL,
                         G_TYPE_DBUS_MESSAGE,
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
