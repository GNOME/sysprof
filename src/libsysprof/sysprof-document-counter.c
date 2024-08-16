/* sysprof-document-counter.c
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

#include <math.h>

#include "timsort/gtktimsortprivate.h"

#include "sysprof-document-counter-private.h"
#include "sysprof-document-counter-value-private.h"
#include "sysprof-document-private.h"

enum {
  PROP_0,
  PROP_CATEGORY,
  PROP_DESCRIPTION,
  PROP_ID,
  PROP_KEY,
  PROP_MAX_VALUE,
  PROP_MIN_VALUE,
  PROP_NAME,
  N_PROPS
};

static guint
sysprof_document_counter_get_n_items (GListModel *model)
{
  return sysprof_document_counter_get_n_values (SYSPROF_DOCUMENT_COUNTER (model));
}

static GType
sysprof_document_counter_get_item_type (GListModel *model)
{
  return SYSPROF_TYPE_DOCUMENT_COUNTER_VALUE;
}

static gpointer
sysprof_document_counter_get_item (GListModel *model,
                                   guint       position)
{
  SysprofDocumentCounter *self = SYSPROF_DOCUMENT_COUNTER (model);
  const SysprofDocumentTimedValue *value;

  if (position >= self->values->len)
    return NULL;

  value = &g_array_index (self->values, SysprofDocumentTimedValue, position);

  return _sysprof_document_counter_value_new (self->type, value, self);
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_n_items = sysprof_document_counter_get_n_items;
  iface->get_item_type = sysprof_document_counter_get_item_type;
  iface->get_item = sysprof_document_counter_get_item;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofDocumentCounter, sysprof_document_counter, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

static GParamSpec *properties [N_PROPS];

static void
sysprof_document_counter_finalize (GObject *object)
{
  SysprofDocumentCounter *self = (SysprofDocumentCounter *)object;

  g_clear_pointer (&self->category, g_ref_string_release);
  g_clear_pointer (&self->description, g_ref_string_release);
  g_clear_pointer (&self->name, g_ref_string_release);
  g_clear_pointer (&self->values, g_array_unref);

  G_OBJECT_CLASS (sysprof_document_counter_parent_class)->finalize (object);
}

static void
sysprof_document_counter_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  SysprofDocumentCounter *self = SYSPROF_DOCUMENT_COUNTER (object);

  switch (prop_id)
    {
    case PROP_CATEGORY:
      g_value_set_string (value, sysprof_document_counter_get_category (self));
      break;

    case PROP_DESCRIPTION:
      g_value_set_string (value, sysprof_document_counter_get_description (self));
      break;

    case PROP_MIN_VALUE:
      g_value_set_double (value, self->min_value);
      break;

    case PROP_MAX_VALUE:
      g_value_set_double (value, self->max_value);
      break;

    case PROP_NAME:
      g_value_set_string (value, sysprof_document_counter_get_name (self));
      break;

    case PROP_KEY:
      g_value_take_string (value, sysprof_document_counter_dup_key (self));
      break;

    case PROP_ID:
      g_value_set_uint (value, sysprof_document_counter_get_id (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_document_counter_class_init (SysprofDocumentCounterClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_document_counter_finalize;
  object_class->get_property = sysprof_document_counter_get_property;

  properties [PROP_ID] =
    g_param_spec_uint ("id", NULL, NULL,
                       0, G_MAXUINT, 0,
                       (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_CATEGORY] =
    g_param_spec_string ("category", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_DESCRIPTION] =
    g_param_spec_string ("description", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_KEY] =
    g_param_spec_string ("key", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_NAME] =
    g_param_spec_string ("name", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_MIN_VALUE] =
    g_param_spec_double ("min-value", NULL, NULL,
                         -G_MAXDOUBLE, G_MAXDOUBLE, 0,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_MAX_VALUE] =
    g_param_spec_double ("max-value", NULL, NULL,
                         -G_MAXDOUBLE, G_MAXDOUBLE, 0,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_document_counter_init (SysprofDocumentCounter *self)
{
}

SysprofDocumentCounter *
_sysprof_document_counter_new (guint       id,
                               guint       type,
                               GRefString *category,
                               GRefString *name,
                               GRefString *description,
                               GArray     *values,
                               gint64      begin_time)
{
  SysprofDocumentCounter *self;

  self = g_object_new (SYSPROF_TYPE_DOCUMENT_COUNTER, NULL);
  self->id = id;
  self->type = type;
  self->category = category;
  self->name = name;
  self->description = description;
  self->values = values;
  self->begin_time = begin_time;

  return self;
}

const char *
sysprof_document_counter_get_category (SysprofDocumentCounter *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_COUNTER (self), NULL);

  return self->category;
}

const char *
sysprof_document_counter_get_description (SysprofDocumentCounter *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_COUNTER (self), NULL);

  if (self->description == NULL || self->description[0] == 0)
    return NULL;

  return self->description;
}

const char *
sysprof_document_counter_get_name (SysprofDocumentCounter *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_COUNTER (self), NULL);

  return self->name;
}

guint
sysprof_document_counter_get_id (SysprofDocumentCounter *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_COUNTER (self), 0);

  return self->id;
}

GType
sysprof_document_counter_get_value_type (SysprofDocumentCounter *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_COUNTER (self), G_TYPE_INVALID);

  if (self->type == SYSPROF_CAPTURE_COUNTER_INT64)
    return G_TYPE_INT64;

  if (self->type == SYSPROF_CAPTURE_COUNTER_DOUBLE)
    return G_TYPE_DOUBLE;

  g_return_val_if_reached (G_TYPE_INVALID);
}

guint
sysprof_document_counter_get_n_values (SysprofDocumentCounter *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_COUNTER (self), 0);

  return self->values->len;
}

void
sysprof_document_counter_get_value (SysprofDocumentCounter *self,
                                    guint                   nth,
                                    gint64                 *time,
                                    GValue                 *value)
{
  g_return_if_fail (SYSPROF_IS_DOCUMENT_COUNTER (self));
  g_return_if_fail (nth < self->values->len);
  g_return_if_fail (value == NULL || G_IS_VALUE (value));

  if (time != NULL)
    *time = g_array_index (self->values, SysprofDocumentTimedValue, nth).time;

  if (value == NULL)
    return;

  if (G_VALUE_HOLDS_INT64 (value))
    g_value_set_int64 (value, g_array_index (self->values, SysprofDocumentTimedValue, nth).v_int64);
  else if (G_VALUE_HOLDS_DOUBLE (value))
    g_value_set_double (value, g_array_index (self->values, SysprofDocumentTimedValue, nth).v_double);
}

gint64
sysprof_document_counter_get_value_int64 (SysprofDocumentCounter *self,
                                          guint                   nth,
                                          gint64                 *time)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_COUNTER (self), 0);
  g_return_val_if_fail (nth < self->values->len, 0);

  if (time != NULL)
    *time = g_array_index (self->values, SysprofDocumentTimedValue, nth).time;

  return g_array_index (self->values, SysprofDocumentTimedValue, nth).v_int64;
}

double
sysprof_document_counter_get_value_double (SysprofDocumentCounter *self,
                                           guint                   nth,
                                           gint64                 *time)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_COUNTER (self), 0);
  g_return_val_if_fail (nth < self->values->len, 0);

  if (time != NULL)
    *time = g_array_index (self->values, SysprofDocumentTimedValue, nth).time;

  return g_array_index (self->values, SysprofDocumentTimedValue, nth).v_double;
}

static inline double
value_as_double_with_correction (guint                      type,
                                 SysprofDocumentTimedValue *value)
{
  if (type == SYSPROF_CAPTURE_COUNTER_DOUBLE)
    {
      /* sanitize awkward data coming over */
      if (value->v_double > (double)G_MAXINT64)
        value->v_double = 0;
      return value->v_double;
    }
  else if (type == SYSPROF_CAPTURE_COUNTER_INT64)
    return value->v_int64;
  else
    return .0;
}

static int
sort_by_time (gconstpointer aptr,
              gconstpointer bptr)
{
  const SysprofDocumentTimedValue *a = aptr;
  const SysprofDocumentTimedValue *b = bptr;

  if (a->time < b->time)
    return -1;

  if (a->time > b->time)
    return 1;

  return 0;
}

void
_sysprof_document_counter_calculate_range (SysprofDocumentCounter *self)
{
  SysprofDocumentTimedValue *values;
  gboolean min_value_changed = FALSE;
  gboolean max_value_changed = FALSE;
  double min_value;
  double max_value;
  guint n_values;

  g_return_if_fail (SYSPROF_IS_DOCUMENT_COUNTER (self));

  if (self->values->len == 0)
    return;

  gtk_tim_sort (self->values->data,
                self->values->len,
                sizeof (SysprofDocumentTimedValue),
                (GCompareDataFunc)sort_by_time,
                NULL);

  values = &g_array_index (self->values, SysprofDocumentTimedValue, 0);
  n_values = self->values->len;

  min_value = value_as_double_with_correction (self->type, &values[0]);
  max_value = min_value;

  for (guint i = 1; i < n_values; i++)
    {
      double value = value_as_double_with_correction (self->type, &values[i]);

      min_value = MIN (min_value, value);
      max_value = MAX (max_value, value);
    }

  min_value_changed = self->min_value != min_value;
  max_value_changed = self->max_value != max_value;

  self->min_value = min_value;
  self->max_value = max_value;

  if (min_value_changed)
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MIN_VALUE]);

  if (max_value_changed)
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_VALUE]);
}

double
sysprof_document_counter_get_max_value (SysprofDocumentCounter *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_COUNTER (self), .0);

  return self->max_value;
}

double
sysprof_document_counter_get_min_value (SysprofDocumentCounter *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_COUNTER (self), .0);

  return self->min_value;
}

char *
sysprof_document_counter_dup_key (SysprofDocumentCounter *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_COUNTER (self), NULL);

  return g_strdup_printf ("%s/%s", self->category, self->name);
}
