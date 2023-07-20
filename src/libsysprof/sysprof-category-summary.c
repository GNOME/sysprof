/* sysprof-category-summary.c
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

#include "sysprof-category-summary-private.h"
#include "sysprof-enums.h"

enum {
  PROP_0,
  PROP_CATEGORY,
  PROP_FRACTION,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofCategorySummary, sysprof_category_summary, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static void
sysprof_category_summary_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  SysprofCategorySummary *self = SYSPROF_CATEGORY_SUMMARY (object);

  switch (prop_id)
    {
    case PROP_CATEGORY:
      g_value_set_enum (value, sysprof_category_summary_get_category (self));
      break;

    case PROP_FRACTION:
      g_value_set_double (value, sysprof_category_summary_get_fraction (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_category_summary_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  SysprofCategorySummary *self = SYSPROF_CATEGORY_SUMMARY (object);

  switch (prop_id)
    {
    case PROP_CATEGORY:
      self->category = g_value_get_enum (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_category_summary_class_init (SysprofCategorySummaryClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = sysprof_category_summary_get_property;
  object_class->set_property = sysprof_category_summary_set_property;

  properties[PROP_CATEGORY] =
    g_param_spec_enum ("category", NULL, NULL,
                      SYSPROF_TYPE_CALLGRAPH_CATEGORY,
                      SYSPROF_CALLGRAPH_CATEGORY_UNCATEGORIZED,
                      (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties[PROP_FRACTION] =
    g_param_spec_double ("fraction", NULL, NULL,
                         0, 1, 0,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_category_summary_init (SysprofCategorySummary *self)
{
}

SysprofCallgraphCategory
sysprof_category_summary_get_category (SysprofCategorySummary *self)
{
  return self->category;
}

double
sysprof_category_summary_get_fraction (SysprofCategorySummary *self)
{
  return CLAMP (self->count / (double)self->total, .0, 1.);
}
