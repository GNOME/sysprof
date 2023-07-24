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

#include "sysprof-document-counter-private.h"
#include "sysprof-document-counter-value-private.h"

struct _SysprofDocumentCounterValue
{
  GObject parent_instance;
  SysprofDocumentCounter *counter;
  SysprofDocumentTimedValue value;
  guint type;
};

enum {
  PROP_0,
  PROP_COUNTER,
  PROP_TIME,
  PROP_TIME_OFFSET,
  PROP_VALUE_DOUBLE,
  PROP_VALUE_INT64,
  PROP_VALUE_STRING,
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
    case PROP_COUNTER:
      g_value_set_object (value, sysprof_document_counter_value_get_counter (self));
      break;

    case PROP_TIME:
      g_value_set_int64 (value, sysprof_document_counter_value_get_time (self));
      break;

    case PROP_TIME_OFFSET:
      g_value_set_int64 (value, sysprof_document_counter_value_get_time_offset (self));
      break;

    case PROP_VALUE_DOUBLE:
      g_value_set_double (value, sysprof_document_counter_value_get_value_double (self));
      break;

    case PROP_VALUE_INT64:
      g_value_set_int64 (value, sysprof_document_counter_value_get_value_int64 (self));
      break;

    case PROP_VALUE_STRING:
      g_value_take_string (value, sysprof_document_counter_value_format (self));
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

  properties [PROP_COUNTER] =
    g_param_spec_object ("counter", NULL, NULL,
                         SYSPROF_TYPE_DOCUMENT_COUNTER,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_TIME] =
    g_param_spec_int64 ("time", NULL, NULL,
                        G_MININT64, G_MAXINT64, 0,
                        (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_TIME_OFFSET] =
    g_param_spec_int64 ("time-offset", NULL, NULL,
                        G_MININT64, G_MAXINT64, 0,
                        (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_VALUE_DOUBLE] =
    g_param_spec_double ("value-double", NULL, NULL,
                         -G_MAXDOUBLE, G_MAXDOUBLE, 0,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_VALUE_INT64] =
    g_param_spec_int64 ("value-int64", NULL, NULL,
                        G_MININT64, G_MAXINT64, 0,
                        (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_VALUE_STRING] =
    g_param_spec_string ("value-string", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_document_counter_value_init (SysprofDocumentCounterValue *self)
{
}

SysprofDocumentCounterValue *
_sysprof_document_counter_value_new (guint                            type,
                                     const SysprofDocumentTimedValue *value,
                                     SysprofDocumentCounter          *counter)
{
  SysprofDocumentCounterValue *self;

  self = g_object_new (SYSPROF_TYPE_DOCUMENT_COUNTER_VALUE, NULL);
  self->type = type;
  self->value = *value;
  self->counter = g_object_ref (counter);

  return self;
}

gint64
sysprof_document_counter_value_get_time (SysprofDocumentCounterValue *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_COUNTER_VALUE (self), 0);

  return self->value.time;
}

gint64
sysprof_document_counter_value_get_time_offset (SysprofDocumentCounterValue *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_COUNTER_VALUE (self), 0);

  return self->value.time - self->counter->begin_time;
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
  if (self->type == SYSPROF_CAPTURE_COUNTER_INT64)
    return self->value.v_int64;
  else
    return (gint64)self->value.v_double;
}

double
sysprof_document_counter_value_get_value_double (SysprofDocumentCounterValue *self)
{
  if (self->type == SYSPROF_CAPTURE_COUNTER_DOUBLE)
    return self->value.v_double;
  else
    return (double)self->value.v_int64;
}

char *
sysprof_document_counter_value_format (SysprofDocumentCounterValue *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_COUNTER_VALUE (self), NULL);

  if (self->type == SYSPROF_CAPTURE_COUNTER_DOUBLE)
    return g_strdup_printf ("%lf", self->value.v_double);
  else
    return g_strdup_printf ("%"G_GINT64_FORMAT, self->value.v_int64);
}

/**
 * sysprof_document_counter_value_get_counter:
 * @self: a #SysprofDocumentCounterValue
 *
 * Returns:  (transfer none): a #SysprofDocumentCounter
 */
SysprofDocumentCounter *
sysprof_document_counter_value_get_counter (SysprofDocumentCounterValue *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_COUNTER_VALUE (self), NULL);

  return self->counter;
}
