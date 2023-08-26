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

#include <glib/gi18n.h>

#include "sysprof-category-summary-private.h"
#include "sysprof-enums.h"

enum {
  PROP_0,
  PROP_CATEGORY,
  PROP_CATEGORY_NAME,
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

    case PROP_CATEGORY_NAME:
      g_value_set_string (value, sysprof_category_summary_get_category_name (self));
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

  properties[PROP_CATEGORY_NAME] =
    g_param_spec_string ("category-name", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

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

const char *
_sysprof_callgraph_category_get_name (SysprofCallgraphCategory category)
{
  switch (category)
    {
    case SYSPROF_CALLGRAPH_CATEGORY_UNCATEGORIZED:
      return _("Uncategorized");

    case SYSPROF_CALLGRAPH_CATEGORY_A11Y:
      return _("Accessibility");

    case SYSPROF_CALLGRAPH_CATEGORY_ACTIONS:
      return _("Actions");

    case SYSPROF_CALLGRAPH_CATEGORY_COREDUMP:
      return _("Crash Handler");

    case SYSPROF_CALLGRAPH_CATEGORY_CONTEXT_SWITCH:
      return _("Context Switches");

    case SYSPROF_CALLGRAPH_CATEGORY_CSS:
      return _("CSS");

    case SYSPROF_CALLGRAPH_CATEGORY_GRAPHICS:
      return _("Graphics");

    case SYSPROF_CALLGRAPH_CATEGORY_ICONS:
      return _("Icons");

    case SYSPROF_CALLGRAPH_CATEGORY_INPUT:
      return _("Input");

    case SYSPROF_CALLGRAPH_CATEGORY_IO:
      return _("IO");

    case SYSPROF_CALLGRAPH_CATEGORY_IPC:
      return _("IPC");

    case SYSPROF_CALLGRAPH_CATEGORY_JAVASCRIPT:
      return _("JavaScript");

    case SYSPROF_CALLGRAPH_CATEGORY_KERNEL:
      return _("Kernel");

    case SYSPROF_CALLGRAPH_CATEGORY_LAYOUT:
      return _("Layout");

    case SYSPROF_CALLGRAPH_CATEGORY_LOCKING:
      return _("Locking");

    case SYSPROF_CALLGRAPH_CATEGORY_MAIN_LOOP:
      return _("Main Loop");

    case SYSPROF_CALLGRAPH_CATEGORY_MEMORY:
      return _("Memory");

    case SYSPROF_CALLGRAPH_CATEGORY_PAINT:
      return _("Paint");

    case SYSPROF_CALLGRAPH_CATEGORY_TYPE_SYSTEM:
      return _("Type System");

    case SYSPROF_CALLGRAPH_CATEGORY_UNWINDABLE:
      return _("Unwindable");

    case SYSPROF_CALLGRAPH_CATEGORY_WINDOWING:
      return _("Windowing");

    case SYSPROF_CALLGRAPH_CATEGORY_PRESENTATION:
    case SYSPROF_CALLGRAPH_CATEGORY_LAST:
    default:
      return NULL;
    }
}

const char *
sysprof_category_summary_get_category_name (SysprofCategorySummary *self)
{
  g_return_val_if_fail (SYSPROF_IS_CATEGORY_SUMMARY (self), NULL);

  return _sysprof_callgraph_category_get_name (self->category);
}
