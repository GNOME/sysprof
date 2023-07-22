/* sysprof-cpu-info.c
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

#include "sysprof-cpu-info.h"

struct _SysprofCpuInfo
{
  GObject parent_instance;
  char *model_name;
  guint id;
  guint core_id;
};

enum {
  PROP_0,
  PROP_ID,
  PROP_CORE_ID,
  PROP_MODEL_NAME,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofCpuInfo, sysprof_cpu_info, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static void
sysprof_cpu_info_finalize (GObject *object)
{
  SysprofCpuInfo *self = (SysprofCpuInfo *)object;

  g_clear_pointer (&self->model_name, g_free);

  G_OBJECT_CLASS (sysprof_cpu_info_parent_class)->finalize (object);
}

static void
sysprof_cpu_info_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  SysprofCpuInfo *self = SYSPROF_CPU_INFO (object);

  switch (prop_id)
    {
    case PROP_ID:
      g_value_set_uint (value, self->id);
      break;

    case PROP_CORE_ID:
      g_value_set_uint (value, self->core_id);
      break;

    case PROP_MODEL_NAME:
      g_value_set_string (value, self->model_name);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_cpu_info_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  SysprofCpuInfo *self = SYSPROF_CPU_INFO (object);

  switch (prop_id)
    {
    case PROP_ID:
      self->id = g_value_get_uint (value);
      break;

    case PROP_CORE_ID:
      self->core_id = g_value_get_uint (value);
      break;

    case PROP_MODEL_NAME:
      self->model_name = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_cpu_info_class_init (SysprofCpuInfoClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_cpu_info_finalize;
  object_class->get_property = sysprof_cpu_info_get_property;
  object_class->set_property = sysprof_cpu_info_set_property;

  properties[PROP_ID] =
    g_param_spec_uint ("id", NULL, NULL,
                       0, G_MAXUINT, 0,
                       (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties[PROP_CORE_ID] =
    g_param_spec_uint ("core-id", NULL, NULL,
                       0, G_MAXUINT, 0,
                       (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties[PROP_MODEL_NAME] =
    g_param_spec_string ("model-name", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_cpu_info_init (SysprofCpuInfo *self)
{
}

const char *
sysprof_cpu_info_get_model_name (SysprofCpuInfo *self)
{
  g_return_val_if_fail (SYSPROF_IS_CPU_INFO (self), NULL);

  return self->model_name;
}

guint
sysprof_cpu_info_get_id (SysprofCpuInfo *self)
{
  g_return_val_if_fail (SYSPROF_IS_CPU_INFO (self), 0);

  return self->id;
}

void
_sysprof_cpu_info_set_model_name (SysprofCpuInfo *self,
                                  const char     *model_name)
{
  g_return_if_fail (SYSPROF_IS_CPU_INFO (self));

  if (g_set_str (&self->model_name, model_name))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODEL_NAME]);
}

guint
sysprof_cpu_info_get_core_id (SysprofCpuInfo *self)
{
  g_return_val_if_fail (SYSPROF_IS_CPU_INFO (self), 0);

  return self->core_id;
}

void
_sysprof_cpu_info_set_core_id (SysprofCpuInfo *self,
                               guint           core_id)
{
  g_return_if_fail (SYSPROF_IS_CPU_INFO (self));

  if (self->core_id != core_id)
    {
      self->core_id = core_id;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CORE_ID]);
    }
}
