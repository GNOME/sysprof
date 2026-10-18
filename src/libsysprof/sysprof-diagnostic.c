/* sysprof-diagnostic.c
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

#include "sysprof-diagnostic-private.h"

typedef struct _SysprofDiagnosticPrivate
{
  char            *domain;
  char            *category;
  char            *message;
  SysprofTimeSpan  time_span;
  GListStore      *frames;
  guint            fatal : 1;
  guint            has_time_span : 1;
} SysprofDiagnosticPrivate;

enum {
  PROP_0,
  PROP_DOMAIN,
  PROP_MESSAGE,
  PROP_FATAL,
  PROP_CATEGORY,
  PROP_TIME_SPAN,
  PROP_FRAMES,
  N_PROPS
};

G_DEFINE_TYPE_WITH_PRIVATE (SysprofDiagnostic, sysprof_diagnostic, G_TYPE_OBJECT)

static GParamSpec *properties[N_PROPS];

static void
sysprof_diagnostic_finalize (GObject *object)
{
  SysprofDiagnosticPrivate *priv = sysprof_diagnostic_get_instance_private (SYSPROF_DIAGNOSTIC (object));

  g_clear_pointer (&priv->domain, g_free);
  g_clear_pointer (&priv->category, g_free);
  g_clear_pointer (&priv->message, g_free);
  g_clear_object (&priv->frames);

  G_OBJECT_CLASS (sysprof_diagnostic_parent_class)->finalize (object);
}

static void
sysprof_diagnostic_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  SysprofDiagnostic *self = SYSPROF_DIAGNOSTIC (object);
  SysprofDiagnosticPrivate *priv = sysprof_diagnostic_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_DOMAIN:
      g_value_set_string (value, priv->domain);
      break;

    case PROP_MESSAGE:
      g_value_set_string (value, priv->message);
      break;

    case PROP_FATAL:
      g_value_set_boolean (value, priv->fatal);
      break;

    case PROP_CATEGORY:
      g_value_set_string (value, priv->category);
      break;

    case PROP_TIME_SPAN:
      g_value_set_boxed (value, priv->has_time_span ? &priv->time_span : NULL);
      break;

    case PROP_FRAMES:
      g_value_set_object (value, priv->frames);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_diagnostic_class_init (SysprofDiagnosticClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_diagnostic_finalize;
  object_class->get_property = sysprof_diagnostic_get_property;

  properties[PROP_DOMAIN] =
    g_param_spec_string ("domain", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_MESSAGE] =
    g_param_spec_string ("message", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_FATAL] =
    g_param_spec_boolean ("fatal", NULL, NULL,
                          FALSE,
                          (G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS));

  properties[PROP_CATEGORY] =
    g_param_spec_string ("category", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_TIME_SPAN] =
    g_param_spec_boxed ("time-span", NULL, NULL,
                        SYSPROF_TYPE_TIME_SPAN,
                        (G_PARAM_READABLE |
                         G_PARAM_STATIC_STRINGS));

  properties[PROP_FRAMES] =
    g_param_spec_object ("frames", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_diagnostic_init (SysprofDiagnostic *self)
{
  SysprofDiagnosticPrivate *priv = sysprof_diagnostic_get_instance_private (self);

  priv->frames = g_list_store_new (SYSPROF_TYPE_DOCUMENT_FRAME);
}

const char *
sysprof_diagnostic_get_domain (SysprofDiagnostic *self)
{
  SysprofDiagnosticPrivate *priv = sysprof_diagnostic_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_DIAGNOSTIC (self), NULL);

  return priv->domain;
}

const char *
sysprof_diagnostic_get_message (SysprofDiagnostic *self)
{
  SysprofDiagnosticPrivate *priv = sysprof_diagnostic_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_DIAGNOSTIC (self), NULL);

  return priv->message;
}

gboolean
sysprof_diagnostic_get_fatal (SysprofDiagnostic *self)
{
  SysprofDiagnosticPrivate *priv = sysprof_diagnostic_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_DIAGNOSTIC (self), FALSE);

  return priv->fatal;
}

/**
 * sysprof_diagnostic_get_category:
 * @self: a #SysprofDiagnostic
 *
 * Gets a stable category identifier for the diagnostic, if available.
 *
 * Returns: (nullable): the category
 */
const char *
sysprof_diagnostic_get_category (SysprofDiagnostic *self)
{
  SysprofDiagnosticPrivate *priv = sysprof_diagnostic_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_DIAGNOSTIC (self), NULL);

  return priv->category;
}

/**
 * sysprof_diagnostic_get_time_span:
 * @self: a #SysprofDiagnostic
 *
 * Gets the capture-clock interval relevant to this diagnostic.
 *
 * Returns: (transfer none) (nullable): the time span
 */
const SysprofTimeSpan *
sysprof_diagnostic_get_time_span (SysprofDiagnostic *self)
{
  SysprofDiagnosticPrivate *priv = sysprof_diagnostic_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_DIAGNOSTIC (self), NULL);

  return priv->has_time_span ? &priv->time_span : NULL;
}

/**
 * sysprof_diagnostic_list_frames:
 * @self: a #SysprofDiagnostic
 *
 * Lists the capture frames that provide evidence for this diagnostic.
 *
 * Returns: (transfer full): a #GListModel of #SysprofDocumentFrame
 */
GListModel *
sysprof_diagnostic_list_frames (SysprofDiagnostic *self)
{
  SysprofDiagnosticPrivate *priv = sysprof_diagnostic_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_DIAGNOSTIC (self), NULL);

  return G_LIST_MODEL (g_object_ref (priv->frames));
}

SysprofDiagnostic *
_sysprof_diagnostic_new (char     *domain,
                         char     *message,
                         gboolean  fatal)
{
  SysprofDiagnostic *self;
  SysprofDiagnosticPrivate *priv;

  self = g_object_new (SYSPROF_TYPE_DIAGNOSTIC, NULL);
  priv = sysprof_diagnostic_get_instance_private (self);
  priv->message = g_steal_pointer (&message);
  priv->category = g_strdup (domain);
  priv->domain = g_steal_pointer (&domain);
  priv->fatal = !!fatal;

  return self;
}

void
_sysprof_diagnostic_set_details (SysprofDiagnostic     *self,
                                 const char            *domain,
                                 const char            *category,
                                 const char            *message,
                                 const SysprofTimeSpan *time_span)
{
  SysprofDiagnosticPrivate *priv = sysprof_diagnostic_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_DIAGNOSTIC (self));

  g_set_str (&priv->domain, domain);
  g_set_str (&priv->category, category);
  g_set_str (&priv->message, message);

  if (time_span != NULL)
    {
      priv->time_span = *time_span;
      priv->has_time_span = TRUE;
    }
}

void
_sysprof_diagnostic_add_frame (SysprofDiagnostic    *self,
                               SysprofDocumentFrame *frame)
{
  SysprofDiagnosticPrivate *priv = sysprof_diagnostic_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_DIAGNOSTIC (self));
  g_return_if_fail (SYSPROF_IS_DOCUMENT_FRAME (frame));

  g_list_store_append (priv->frames, frame);
}
