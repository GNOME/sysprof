/* sysprof-preload-source.c
 *
 * Copyright 2020 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sysprof-preload-source.h"

#include "config.h"

#include "sysprof-preload-source.h"

struct _SysprofPreloadSource
{
  GObject parent_instance;
  gchar *preload;
};

enum {
  PROP_0,
  PROP_PRELOAD,
  N_PROPS
};

static void
sysprof_preload_source_modify_spawn (SysprofSource    *source,
                                     SysprofSpawnable *spawnable)
{
  SysprofPreloadSource *self = (SysprofPreloadSource *)source;

  g_assert (SYSPROF_IS_SOURCE (self));
  g_assert (SYSPROF_IS_SPAWNABLE (spawnable));

#ifdef __linux__
  if (self->preload)
    {
      g_autofree gchar *freeme = NULL;
      const gchar *ld_preload;

      if (!(ld_preload = sysprof_spawnable_getenv (spawnable, "LD_PRELOAD")))
        sysprof_spawnable_setenv (spawnable, "LD_PRELOAD", self->preload);
      else
        sysprof_spawnable_setenv (spawnable,
                                  "LD_PRELOAD",
                                  (freeme = g_strdup_printf ("%s,%s", self->preload, ld_preload)));
    }
#endif
}

static void
sysprof_preload_source_stop (SysprofSource *source)
{
  sysprof_source_emit_finished (source);
}

static void
source_iface_init (SysprofSourceInterface *iface)
{
  iface->modify_spawn = sysprof_preload_source_modify_spawn;
  iface->stop = sysprof_preload_source_stop;
}

G_DEFINE_TYPE_WITH_CODE (SysprofPreloadSource, sysprof_preload_source, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (SYSPROF_TYPE_SOURCE, source_iface_init))

static GParamSpec *properties [N_PROPS];

static void
sysprof_preload_source_finalize (GObject *object)
{
  SysprofPreloadSource *self = (SysprofPreloadSource *)object;

  g_clear_pointer (&self->preload, g_free);

  G_OBJECT_CLASS (sysprof_preload_source_parent_class)->finalize (object);
}

static void
sysprof_preload_source_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  SysprofPreloadSource *self = SYSPROF_PRELOAD_SOURCE (object);

  switch (prop_id)
    {
    case PROP_PRELOAD:
      g_value_set_string (value, self->preload);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_preload_source_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  SysprofPreloadSource *self = SYSPROF_PRELOAD_SOURCE (object);

  switch (prop_id)
    {
    case PROP_PRELOAD:
      g_free (self->preload);
      self->preload = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_preload_source_class_init (SysprofPreloadSourceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_preload_source_finalize;
  object_class->get_property = sysprof_preload_source_get_property;
  object_class->set_property = sysprof_preload_source_set_property;

  properties [PROP_PRELOAD] =
    g_param_spec_string ("preload",
                         "Preload",
                         "The preload to load into the process",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  
  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_preload_source_init (SysprofPreloadSource *self)
{
}
