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

#include "sysprof-document-counter.h"

struct _SysprofDocumentCounter
{
  GObject parent_instance;
  GRefString *category;
  GRefString *description;
  GRefString *name;
  guint id;
};

enum {
  PROP_0,
  PROP_CATEGORY,
  PROP_DESCRIPTION,
  PROP_ID,
  PROP_NAME,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofDocumentCounter, sysprof_document_counter, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static void
sysprof_document_counter_finalize (GObject *object)
{
  SysprofDocumentCounter *self = (SysprofDocumentCounter *)object;

  g_clear_pointer (&self->category, g_ref_string_release);
  g_clear_pointer (&self->description, g_ref_string_release);
  g_clear_pointer (&self->name, g_ref_string_release);

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

    case PROP_NAME:
      g_value_set_string (value, sysprof_document_counter_get_name (self));
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

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_document_counter_init (SysprofDocumentCounter *self)
{
}

SysprofDocumentCounter *
_sysprof_document_counter_new (guint       id,
                               GRefString *category,
                               GRefString *name,
                               GRefString *description)
{
  SysprofDocumentCounter *self;

  self = g_object_new (SYSPROF_TYPE_DOCUMENT_COUNTER, NULL);
  self->id = id;
  self->category = category;
  self->name = name;
  self->description = description;

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
