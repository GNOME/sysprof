/* sysprof-document-log.c
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
#include "sysprof-document-log.h"

struct _SysprofDocumentLog
{
  SysprofDocumentFrame parent_instance;
};

struct _SysprofDocumentLogClass
{
  SysprofDocumentFrameClass parent_class;
};

enum {
  PROP_0,
  PROP_DOMAIN,
  PROP_MESSAGE,
  PROP_SEVERITY,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofDocumentLog, sysprof_document_log, SYSPROF_TYPE_DOCUMENT_FRAME)

static GParamSpec *properties [N_PROPS];

static void
sysprof_document_log_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  SysprofDocumentLog *self = SYSPROF_DOCUMENT_LOG (object);

  switch (prop_id)
    {
    case PROP_SEVERITY:
      g_value_set_uint (value, sysprof_document_log_get_severity (self));
      break;

    case PROP_MESSAGE:
      g_value_set_string (value, sysprof_document_log_get_message (self));
      break;

    case PROP_DOMAIN:
      g_value_set_string (value, sysprof_document_log_get_domain (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_document_log_class_init (SysprofDocumentLogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofDocumentFrameClass *document_frame_class = SYSPROF_DOCUMENT_FRAME_CLASS (klass);

  object_class->get_property = sysprof_document_log_get_property;

  document_frame_class->type_name = N_("Log");

  properties [PROP_SEVERITY] =
    g_param_spec_uint ("severity", NULL, NULL,
                       0, G_MAXUINT16, 0,
                       (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_DOMAIN] =
    g_param_spec_string ("domain", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_MESSAGE] =
    g_param_spec_string ("message", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_document_log_init (SysprofDocumentLog *self)
{
}

GLogLevelFlags
sysprof_document_log_get_severity (SysprofDocumentLog *self)
{
  const SysprofCaptureLog *log;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_LOG (self), 0);

  log = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureLog);

  return SYSPROF_DOCUMENT_FRAME_UINT16 (self, log->severity);
}

const char *
sysprof_document_log_get_message (SysprofDocumentLog *self)
{
  const SysprofCaptureLog *log;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_LOG (self), 0);

  log = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureLog);

  return SYSPROF_DOCUMENT_FRAME_CSTRING (self, log->message);
}

const char *
sysprof_document_log_get_domain (SysprofDocumentLog *self)
{
  const SysprofCaptureLog *log;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_LOG (self), 0);

  log = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureLog);

  return SYSPROF_DOCUMENT_FRAME_CSTRING (self, log->domain);
}
