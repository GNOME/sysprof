/* sysprof-proxy-source.c
 *
 * Copyright 2019 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sysprof-proxy-source"

#include "config.h"

#include "sysprof-proxy-source.h"

struct _SysprofProxySource
{
  GObject parent_instance;
  gchar *bus_name;
  gchar *object_path;
  GBusType bus_type;
};

static void
sysprof_proxy_source_prepare (SysprofSource *source)
{
  SysprofProxySource *self = (SysprofProxySource *)source;

  g_assert (SYSPROF_IS_PROXY_SOURCE (self));
}

static gboolean
sysprof_proxy_source_get_is_ready (SysprofSource *source)
{
  SysprofProxySource *self = (SysprofProxySource *)source;

  g_assert (SYSPROF_IS_PROXY_SOURCE (self));

  return TRUE;
}

static void
sysprof_proxy_source_set_writer (SysprofSource        *source,
                                 SysprofCaptureWriter *writer)
{
  SysprofProxySource *self = (SysprofProxySource *)source;

  g_assert (SYSPROF_IS_PROXY_SOURCE (self));
  g_assert (writer != NULL);

}

static void
source_iface_init (SysprofSourceInterface *iface)
{
  iface->prepare = sysprof_proxy_source_prepare;
  iface->set_writer = sysprof_proxy_source_set_writer;
  iface->get_is_ready = sysprof_proxy_source_get_is_ready;
}

G_DEFINE_TYPE_WITH_CODE (SysprofProxySource, sysprof_proxy_source, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (SYSPROF_TYPE_SOURCE, source_iface_init))

static void
sysprof_proxy_source_finalize (GObject *object)
{
  SysprofProxySource *self = (SysprofProxySource *)object;

  g_clear_pointer (&self->bus_name, g_free);
  g_clear_pointer (&self->object_path, g_free);

  G_OBJECT_CLASS (sysprof_proxy_source_parent_class)->finalize (object);
}

static void
sysprof_proxy_source_class_init (SysprofProxySourceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_proxy_source_finalize;
}

static void
sysprof_proxy_source_init (SysprofProxySource *self)
{
}

SysprofSource *
sysprof_proxy_source_new (GBusType     bus_type,
                          const gchar *bus_name,
                          const gchar *object_path)
{
  SysprofProxySource *self;

  g_return_val_if_fail (bus_type == G_BUS_TYPE_SESSION || bus_type == G_BUS_TYPE_SYSTEM, NULL);
  g_return_val_if_fail (bus_name != NULL, NULL);
  g_return_val_if_fail (object_path != NULL, NULL);

  self = g_object_new (SYSPROF_TYPE_PROXY_SOURCE, NULL);
  self->bus_type = bus_type;
  self->bus_name = g_strdup (bus_name);
  self->object_path = g_strdup (object_path);

  return SYSPROF_SOURCE (g_steal_pointer (&self));
}
