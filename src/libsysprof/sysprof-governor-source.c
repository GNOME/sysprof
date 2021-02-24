/* sysprof-governor-source.c
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

#define G_LOG_DOMAIN "sysprof-governor-source"

#include "config.h"

#include "sysprof-governor-source.h"
#include "sysprof-helpers.h"

struct _SysprofGovernorSource
{
  GObject parent_instance;
  gchar *old_governor;
  int old_paranoid;
  guint disable_governor : 1;
};

enum {
  PROP_0,
  PROP_DISABLE_GOVERNOR,
  N_PROPS
};

static void source_iface_init (SysprofSourceInterface *iface);

G_DEFINE_TYPE_WITH_CODE (SysprofGovernorSource, sysprof_governor_source, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (SYSPROF_TYPE_SOURCE, source_iface_init))

static GParamSpec *properties [N_PROPS];

static void
sysprof_governor_source_finalize (GObject *object)
{
  SysprofGovernorSource *self = (SysprofGovernorSource *)object;

  g_clear_pointer (&self->old_governor, g_free);

  G_OBJECT_CLASS (sysprof_governor_source_parent_class)->finalize (object);
}

static void
sysprof_governor_source_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  SysprofGovernorSource *self = SYSPROF_GOVERNOR_SOURCE (object);

  switch (prop_id)
    {
    case PROP_DISABLE_GOVERNOR:
      g_value_set_boolean (value, sysprof_governor_source_get_disable_governor (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_governor_source_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  SysprofGovernorSource *self = SYSPROF_GOVERNOR_SOURCE (object);

  switch (prop_id)
    {
    case PROP_DISABLE_GOVERNOR:
      sysprof_governor_source_set_disable_governor (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_governor_source_class_init (SysprofGovernorSourceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_governor_source_finalize;
  object_class->get_property = sysprof_governor_source_get_property;
  object_class->set_property = sysprof_governor_source_set_property;

  properties [PROP_DISABLE_GOVERNOR] =
    g_param_spec_boolean ("disable-governor",
                          "Disable Governor",
                          "Disable Governor",
                          TRUE,
                          (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_governor_source_init (SysprofGovernorSource *self)
{
  self->disable_governor = FALSE;
  self->old_paranoid = 2;
}

SysprofSource *
sysprof_governor_source_new (void)
{
  return g_object_new (SYSPROF_TYPE_GOVERNOR_SOURCE, NULL);
}

gboolean
sysprof_governor_source_get_disable_governor (SysprofGovernorSource *self)
{
  g_return_val_if_fail (SYSPROF_IS_GOVERNOR_SOURCE (self), FALSE);

  return self->disable_governor;
}

void
sysprof_governor_source_set_disable_governor (SysprofGovernorSource *self,
                                              gboolean               disable_governor)
{
  g_return_if_fail (SYSPROF_IS_GOVERNOR_SOURCE (self));

  disable_governor = !!disable_governor;

  if (disable_governor != self->disable_governor)
    {
      self->disable_governor = disable_governor;
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_DISABLE_GOVERNOR]);
    }
}

static void
sysprof_governor_source_serialize (SysprofSource *source,
                                   GKeyFile      *keyfile,
                                   const gchar   *group)
{
  SysprofGovernorSource *self = (SysprofGovernorSource *)source;

  g_assert (SYSPROF_IS_GOVERNOR_SOURCE (self));
  g_assert (keyfile != NULL);
  g_assert (group != NULL);

  g_key_file_set_boolean (keyfile, group, "disable-governor", self->disable_governor);
}

static void
sysprof_governor_source_deserialize (SysprofSource *source,
                                     GKeyFile      *keyfile,
                                     const gchar   *group)
{
  SysprofGovernorSource *self = (SysprofGovernorSource *)source;

  g_assert (SYSPROF_IS_GOVERNOR_SOURCE (self));
  g_assert (keyfile != NULL);
  g_assert (group != NULL);

  sysprof_governor_source_set_disable_governor (self,
                                                g_key_file_get_boolean (keyfile, group, "disable-governor", NULL));
}

static void
disable_governor_cb (GObject      *object,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  SysprofHelpers *helpers = (SysprofHelpers *)object;
  g_autoptr(SysprofGovernorSource) self = user_data;
  g_autoptr(GError) error = NULL;
  g_autofree gchar *old_governor = NULL;

  g_assert (SYSPROF_IS_HELPERS (helpers));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (SYSPROF_IS_GOVERNOR_SOURCE (self));

  if (!sysprof_helpers_set_governor_finish (helpers, result, &old_governor, &error))
    g_warning ("Failed to change governor: %s", error->message);
  else
    self->old_governor = g_steal_pointer (&old_governor);

  sysprof_source_emit_ready (SYSPROF_SOURCE (self));
}

static void
disable_paranoid_cb (GObject      *object,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  SysprofHelpers *helpers = (SysprofHelpers *)object;
  g_autoptr(SysprofGovernorSource) self = user_data;
  g_autoptr(GError) error = NULL;
  int old_paranoid;

  g_assert (SYSPROF_IS_HELPERS (helpers));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (SYSPROF_IS_GOVERNOR_SOURCE (self));

  if (!sysprof_helpers_set_paranoid_finish (helpers, result, &old_paranoid, &error))
    g_debug ("Failed to change perf_event_paranoid: %s", error->message);
  else
    self->old_paranoid = old_paranoid;

  if (!self->disable_governor)
    sysprof_source_emit_ready (SYSPROF_SOURCE (self));
  else
    sysprof_helpers_set_governor_async (helpers,
                                        "performance",
                                        NULL,
                                        disable_governor_cb,
                                        g_steal_pointer (&self));
}

static void
sysprof_governor_source_prepare (SysprofSource *source)
{
  SysprofGovernorSource *self = (SysprofGovernorSource *)source;
  SysprofHelpers *helpers = sysprof_helpers_get_default ();

  g_assert (SYSPROF_IS_GOVERNOR_SOURCE (self));

  sysprof_helpers_set_paranoid_async (helpers,
                                      -1,
                                      NULL,
                                      disable_paranoid_cb,
                                      g_object_ref (self));
}

static void
enable_governor_cb (GObject      *object,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  SysprofHelpers *helpers = (SysprofHelpers *)object;
  g_autoptr(SysprofGovernorSource) self = user_data;
  g_autoptr(GError) error = NULL;
  g_autofree gchar *old_governor = NULL;

  g_assert (SYSPROF_IS_HELPERS (helpers));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (SYSPROF_IS_GOVERNOR_SOURCE (self));

  if (!sysprof_helpers_set_governor_finish (helpers, result, &old_governor, &error))
    g_warning ("Failed to change governor: %s", error->message);

  g_clear_pointer (&self->old_governor, g_free);

  sysprof_source_emit_finished (SYSPROF_SOURCE (self));
}

static void
enable_paranoid_cb (GObject      *object,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  SysprofHelpers *helpers = (SysprofHelpers *)object;
  g_autoptr(SysprofGovernorSource) self = user_data;
  g_autoptr(GError) error = NULL;
  int old_governor;

  g_assert (SYSPROF_IS_HELPERS (helpers));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (SYSPROF_IS_GOVERNOR_SOURCE (self));

  if (!sysprof_helpers_set_paranoid_finish (helpers, result, &old_governor, &error))
    g_debug ("Failed to change event_perf_paranoid: %s", error->message);

  if (!self->disable_governor)
    sysprof_source_emit_finished (SYSPROF_SOURCE (self));
  else
    sysprof_helpers_set_governor_async (helpers,
                                        self->old_governor,
                                        NULL,
                                        enable_governor_cb,
                                        g_steal_pointer (&self));
}

static void
sysprof_governor_source_stop (SysprofSource *source)
{
  SysprofGovernorSource *self = (SysprofGovernorSource *)source;
  SysprofHelpers *helpers = sysprof_helpers_get_default ();

  g_assert (SYSPROF_IS_GOVERNOR_SOURCE (self));

  sysprof_helpers_set_paranoid_async (helpers,
                                      self->old_paranoid,
                                      NULL,
                                      enable_paranoid_cb,
                                      g_object_ref (self));
}

static void
source_iface_init (SysprofSourceInterface *iface)
{
  iface->deserialize = sysprof_governor_source_deserialize;
  iface->prepare = sysprof_governor_source_prepare;
  iface->serialize = sysprof_governor_source_serialize;
  iface->stop = sysprof_governor_source_stop;
}
