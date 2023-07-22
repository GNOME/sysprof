/* sysprof-chart-layer-factory.c
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

#include "sysprof-chart-layer-factory.h"
#include "sysprof-chart-layer-item.h"

struct _SysprofChartLayerFactory
{
  GObject parent_instance;
  GtkBuilderScope *scope;
  char *resource;
  GBytes *bytes;
};

enum {
  PROP_0,
  PROP_BYTES,
  PROP_RESOURCE,
  PROP_SCOPE,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofChartLayerFactory, sysprof_chart_layer_factory, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

void
_sysprof_chart_layer_factory_setup (SysprofChartLayerFactory *self,
                                    SysprofChartLayerItem    *item)
{
  g_autoptr(GtkBuilder) builder = NULL;
  g_autoptr(GError) error = NULL;

  g_return_if_fail (SYSPROF_IS_CHART_LAYER_FACTORY (self));
  g_return_if_fail (G_IS_OBJECT (item));
  g_return_if_fail (self->bytes != NULL);

  builder = gtk_builder_new ();

  gtk_builder_set_current_object (builder, G_OBJECT (item));

  if (self->scope)
    gtk_builder_set_scope (builder, self->scope);

  /* XXX: This is private API, we might need it
   * gtk_builder_set_allow_template_parents (builder, TRUE);
   */

  if (!gtk_builder_extend_with_template (builder, G_OBJECT (item), G_OBJECT_TYPE (item),
                                         (const char *)g_bytes_get_data (self->bytes, NULL),
                                         g_bytes_get_size (self->bytes),
                                         &error))
    g_critical ("Error building template for chart layer: %s", error->message);
}

static void
sysprof_chart_layer_factory_constructed (GObject *object)
{
  SysprofChartLayerFactory *self = (SysprofChartLayerFactory *)object;

  G_OBJECT_CLASS (sysprof_chart_layer_factory_parent_class)->constructed (object);

  if (self->resource != NULL && self->bytes == NULL)
    {
      self->bytes = g_resources_lookup_data (self->resource, 0, NULL);

      if (self->bytes == NULL)
        g_warning ("Failed to locate resource at %s", self->resource);
    }

  if (self->bytes == NULL)
    g_warning ("SysprofChartLayerFactory created without a template");
}

static void
sysprof_chart_layer_factory_finalize (GObject *object)
{
  SysprofChartLayerFactory *self = (SysprofChartLayerFactory *)object;

  g_clear_object (&self->scope);
  g_clear_pointer (&self->resource, g_free);
  g_clear_pointer (&self->bytes, g_bytes_unref);

  G_OBJECT_CLASS (sysprof_chart_layer_factory_parent_class)->finalize (object);
}

static void
sysprof_chart_layer_factory_get_property (GObject    *object,
                                          guint       prop_id,
                                          GValue     *value,
                                          GParamSpec *pspec)
{
  SysprofChartLayerFactory *self = SYSPROF_CHART_LAYER_FACTORY (object);

  switch (prop_id)
    {
    case PROP_SCOPE:
      g_value_set_object (value, self->scope);
      break;

    case PROP_RESOURCE:
      g_value_set_string (value, self->resource);
      break;

    case PROP_BYTES:
      g_value_set_boxed (value, self->bytes);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_chart_layer_factory_set_property (GObject      *object,
                                          guint         prop_id,
                                          const GValue *value,
                                          GParamSpec   *pspec)
{
  SysprofChartLayerFactory *self = SYSPROF_CHART_LAYER_FACTORY (object);

  switch (prop_id)
    {
    case PROP_BYTES:
      self->bytes = g_value_dup_boxed (value);
      break;

    case PROP_RESOURCE:
      self->resource = g_value_dup_string (value);
      break;

    case PROP_SCOPE:
      self->scope = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_chart_layer_factory_class_init (SysprofChartLayerFactoryClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = sysprof_chart_layer_factory_constructed;
  object_class->finalize = sysprof_chart_layer_factory_finalize;
  object_class->get_property = sysprof_chart_layer_factory_get_property;
  object_class->set_property = sysprof_chart_layer_factory_set_property;

  properties[PROP_SCOPE] =
    g_param_spec_object ("scope", NULL, NULL,
                         GTK_TYPE_BUILDER_SCOPE,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties[PROP_RESOURCE] =
    g_param_spec_string ("resource", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties[PROP_BYTES] =
    g_param_spec_boxed ("bytes", NULL, NULL,
                        G_TYPE_BYTES,
                        (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_chart_layer_factory_init (SysprofChartLayerFactory *self)
{
}

SysprofChartLayerFactory *
sysprof_chart_layer_factory_new_from_bytes (GtkBuilderScope *scope,
                                            GBytes          *bytes)
{
  g_return_val_if_fail (!scope || GTK_IS_BUILDER_SCOPE (scope), NULL);
  g_return_val_if_fail (bytes != NULL, NULL);

  return g_object_new (SYSPROF_TYPE_CHART_LAYER_FACTORY,
                       "scope", scope,
                       "bytes", bytes,
                       NULL);
}

SysprofChartLayerFactory *
sysprof_chart_layer_factory_new_from_resource (GtkBuilderScope *scope,
                                               const char      *resource)
{
  g_return_val_if_fail (!scope || GTK_IS_BUILDER_SCOPE (scope), NULL);
  g_return_val_if_fail (resource != NULL, NULL);

  return g_object_new (SYSPROF_TYPE_CHART_LAYER_FACTORY,
                       "scope", scope,
                       "resource", resource,
                       NULL);
}
