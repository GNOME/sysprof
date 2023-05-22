/* sysprof-document-counter-value.c
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

#include "sysprof-document-counter-value-private.h"

struct _SysprofDocumentCounterValue
{
  GObject parent_instance;
  SysprofDocumentTimedValue value;
};

enum {
  PROP_0,
  PROP_TIME,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofDocumentCounterValue, sysprof_document_counter_value, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static void
sysprof_document_counter_value_get_property (GObject    *object,
                                             guint       prop_id,
                                             GValue     *value,
                                             GParamSpec *pspec)
{
  SysprofDocumentCounterValue *self = SYSPROF_DOCUMENT_COUNTER_VALUE (object);

  switch (prop_id)
    {
    case PROP_TIME:
      g_value_set_int64 (value, sysprof_document_counter_value_get_time (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_document_counter_value_class_init (SysprofDocumentCounterValueClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = sysprof_document_counter_value_get_property;

  properties [PROP_TIME] =
    g_param_spec_int64 ("time", NULL, NULL,
                        G_MININT64, 0, G_MAXINT64,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_document_counter_value_init (SysprofDocumentCounterValue *self)
{
}

SysprofDocumentCounterValue *
_sysprof_document_counter_value_new (const SysprofDocumentTimedValue *value)
{
  SysprofDocumentCounterValue *self;

  self = g_object_new (SYSPROF_TYPE_DOCUMENT_COUNTER_VALUE, NULL);
  self->value = *value;

  return self;
}

gint64
sysprof_document_counter_value_get_time (SysprofDocumentCounterValue *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_COUNTER_VALUE (self), 0);

  return self->value.time;
}

void
sysprof_document_counter_value_get_value (SysprofDocumentCounterValue *self,
                                          GValue                      *value)
{
  g_return_if_fail (SYSPROF_IS_DOCUMENT_COUNTER_VALUE (self));
  g_return_if_fail (G_IS_VALUE (value));

  if (G_VALUE_HOLDS_INT64 (value))
    g_value_set_int64 (value, self->value.v_int64);
  else if (G_VALUE_HOLDS_DOUBLE (value))
    g_value_set_double (value, self->value.v_double);
  else
    g_warning_once ("Unsupported value type %s", G_VALUE_TYPE_NAME (value));
}

gint64
sysprof_document_counter_value_get_value_int64 (SysprofDocumentCounterValue *self)
{
  return self->value.v_int64;
}

double
sysprof_document_counter_value_get_value_double (SysprofDocumentCounterValue *self)
{
  return self->value.v_double;
}
