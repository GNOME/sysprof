/* sysprof-mark-detail.c
 *
 * Copyright 2022 Corentin Noël <corentin.noel@collabora.com>
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

#define G_LOG_DOMAIN "sysprof-mark-detail"

#include "config.h"

#include <glib/gi18n.h>

#include "sysprof-mark-detail.h"

struct _SysprofMarkDetail
{
  GObject parent_instance;

  gchar  *label;
  gint64  min;
  gint64  max;
  gint64  average;
  gint64  hits;
};

G_DEFINE_TYPE (SysprofMarkDetail, sysprof_mark_detail, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_LABEL,
  PROP_MIN,
  PROP_MAX,
  PROP_AVERAGE,
  PROP_HITS,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

/**
 * sysprof_mark_detail_new:
 *
 * Create a new #SysprofMarkDetail.
 *
 * Returns: (transfer full): a newly created #SysprofMarkDetail
 */
SysprofMarkDetail *
sysprof_mark_detail_new (const gchar *mark,
                         gint64       min,
                         gint64       max,
                         gint64       avg,
                         gint64       hits)
{
  return g_object_new (SYSPROF_TYPE_MARK_DETAIL,
                       "label", mark,
                       "min", min,
                       "max", max,
                       "average", avg,
                       "hits", hits,
                       NULL);
}

static void
sysprof_mark_detail_finalize (GObject *object)
{
  SysprofMarkDetail *self = (SysprofMarkDetail *)object;

  g_clear_pointer (&self->label, g_free);

  G_OBJECT_CLASS (sysprof_mark_detail_parent_class)->finalize (object);
}

static void
sysprof_mark_detail_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  SysprofMarkDetail *self = SYSPROF_MARK_DETAIL(object);

  switch (prop_id)
    {
    case PROP_LABEL:
      g_value_set_string (value, self->label);
      break;

    case PROP_MIN:
      g_value_set_int64 (value, self->min);
      break;

    case PROP_MAX:
      g_value_set_int64 (value, self->max);
      break;

    case PROP_AVERAGE:
      g_value_set_int64 (value, self->average);
      break;

    case PROP_HITS:
      g_value_set_int64 (value, self->hits);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_mark_detail_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  SysprofMarkDetail *self = SYSPROF_MARK_DETAIL(object);

  switch (prop_id)
    {
    case PROP_LABEL:
      g_assert(self->label == NULL);
      self->label = g_value_dup_string (value);
      break;

    case PROP_MIN:
      self->min = g_value_get_int64 (value);
      break;

    case PROP_MAX:
      self->max = g_value_get_int64 (value);
      break;

    case PROP_AVERAGE:
      self->average = g_value_get_int64 (value);
      break;

    case PROP_HITS:
      self->hits = g_value_get_int64 (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_mark_detail_class_init (SysprofMarkDetailClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_mark_detail_finalize;
  object_class->get_property = sysprof_mark_detail_get_property;
  object_class->set_property = sysprof_mark_detail_set_property;

  properties [PROP_LABEL] =
    g_param_spec_string ("label",
                         "Label",
                         "The label of the mark",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_MIN] =
    g_param_spec_int64 ("min",
                        "Min",
                        "The minimal timespan",
                        0, G_MAXINT64, 0,
                        (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_MAX] =
    g_param_spec_int64 ("max",
                        "max",
                        "The maximal timespan",
                        0, G_MAXINT64, 0,
                        (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_AVERAGE] =
    g_param_spec_int64 ("average",
                        "Average",
                        "The average timespan",
                        0, G_MAXINT64, 0,
                        (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_HITS] =
    g_param_spec_int64 ("hits",
                        "Hits",
                        "The number of hits",
                        0, G_MAXINT64, 0,
                        (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_mark_detail_init (SysprofMarkDetail *self)
{
}
