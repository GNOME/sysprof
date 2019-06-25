/* sysprof-theme-manager.c
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sysprof-theme-manager"

#include "config.h"

#include "sysprof-theme-manager.h"

struct _SysprofThemeManager
{
  GObject     parent_instance;
  GHashTable *theme_resources;
  guint       reload_source;
  guint       registered_signals : 1;
};

typedef struct
{
  guint  id;
  gchar *key;
  gchar *theme_name;
  gchar *variant;
  gchar *resource;
  GtkCssProvider *provider;
} ThemeResource;

G_DEFINE_TYPE (SysprofThemeManager, sysprof_theme_manager, G_TYPE_OBJECT)

static void
theme_resource_free (gpointer data)
{
  ThemeResource *theme_resource = data;

  if (theme_resource != NULL)
    {
      g_clear_pointer (&theme_resource->key, g_free);
      g_clear_pointer (&theme_resource->theme_name, g_free);
      g_clear_pointer (&theme_resource->variant, g_free);
      g_clear_pointer (&theme_resource->resource, g_free);

      if (theme_resource->provider != NULL)
        {
          gtk_style_context_remove_provider_for_screen (gdk_screen_get_default (),
                                                        GTK_STYLE_PROVIDER (theme_resource->provider));
          g_clear_object (&theme_resource->provider);
        }

      g_slice_free (ThemeResource, theme_resource);
    }
}

static gboolean
theme_resource_matches (ThemeResource *theme_resource,
                        GtkSettings   *settings)
{
  g_autofree gchar *theme_name = NULL;
  gboolean dark_theme = FALSE;

  g_assert (theme_resource != NULL);
  g_assert (GTK_IS_SETTINGS (settings));

  if (theme_resource->theme_name == NULL)
    return TRUE;

  g_object_get (settings,
                "gtk-theme-name", &theme_name,
                "gtk-application-prefer-dark-theme", &dark_theme,
                NULL);

  if (g_strcmp0 (theme_name, theme_resource->theme_name) == 0)
    {
      if (dark_theme && g_strcmp0 ("dark", theme_resource->variant) == 0)
        return TRUE;

      if (!dark_theme && (!theme_resource->variant || g_strcmp0 ("light", theme_resource->variant) == 0))
        return TRUE;
    }

  return FALSE;
}

static gboolean
sysprof_theme_manager_do_reload (gpointer data)
{
  SysprofThemeManager *self = data;
  ThemeResource *theme_resource;
  GHashTableIter iter;
  GtkSettings *settings;

  g_assert (SYSPROF_IS_THEME_MANAGER (self));

  self->reload_source = 0;

  settings = gtk_settings_get_default ();

  g_hash_table_iter_init (&iter, self->theme_resources);

  while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&theme_resource))
    {
      if (theme_resource_matches (theme_resource, settings))
        {
          if (theme_resource->provider == NULL)
            {
              theme_resource->provider = gtk_css_provider_new ();
              gtk_css_provider_load_from_resource (theme_resource->provider, theme_resource->resource);
              gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                                         GTK_STYLE_PROVIDER (theme_resource->provider),
                                                         GTK_STYLE_PROVIDER_PRIORITY_APPLICATION - 1);
            }
        }
      else
        {
          if (theme_resource->provider != NULL)
            {
              gtk_style_context_remove_provider_for_screen (gdk_screen_get_default (),
                                                            GTK_STYLE_PROVIDER (theme_resource->provider));
              g_clear_object (&theme_resource->provider);
            }
        }
    }

  return G_SOURCE_REMOVE;
}

static void
sysprof_theme_manager_queue_reload (SysprofThemeManager *self)
{
  g_assert (SYSPROF_IS_THEME_MANAGER (self));

  if (self->reload_source == 0)
    self->reload_source = gdk_threads_add_idle_full (G_PRIORITY_LOW,
                                                     sysprof_theme_manager_do_reload,
                                                     self,
                                                     NULL);
}

static void
sysprof_theme_manager_finalize (GObject *object)
{
  SysprofThemeManager *self = (SysprofThemeManager *)object;

  if (self->reload_source != 0)
    {
      g_source_remove (self->reload_source);
      self->reload_source = 0;
    }

  g_clear_pointer (&self->theme_resources, g_hash_table_unref);

  G_OBJECT_CLASS (sysprof_theme_manager_parent_class)->finalize (object);
}

static void
sysprof_theme_manager_class_init (SysprofThemeManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_theme_manager_finalize;
}

static void
sysprof_theme_manager_init (SysprofThemeManager *self)
{
  self->theme_resources = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, theme_resource_free);

  gtk_icon_theme_add_resource_path (gtk_icon_theme_get_default (), "/org/gnome/sysprof/icons");
}

/**
 * sysprof_theme_manager_get_default:
 *
 * Returns: (transfer none): An #SysprofThemeManager
 */
SysprofThemeManager *
sysprof_theme_manager_get_default (void)
{
  static SysprofThemeManager *instance;

  if (instance == NULL)
    instance = g_object_new (SYSPROF_TYPE_THEME_MANAGER, NULL);

  return instance;
}

guint
sysprof_theme_manager_register_resource (SysprofThemeManager *self,
                                         const gchar         *theme_name,
                                         const gchar         *variant,
                                         const gchar         *resource)
{
  ThemeResource *theme_resource;
  static guint counter;
  guint id;

  g_return_val_if_fail (SYSPROF_IS_THEME_MANAGER (self), 0);

  theme_resource = g_slice_new0 (ThemeResource);
  theme_resource->id = id = ++counter;
  theme_resource->key = g_strdup_printf ("%s-%s-%d",
                                         theme_name ? theme_name : "shared",
                                         variant ? variant : "light",
                                         theme_resource->id);
  theme_resource->theme_name = g_strdup (theme_name);
  theme_resource->variant = g_strdup (variant);
  theme_resource->resource = g_strdup (resource);
  theme_resource->provider = NULL;

  g_hash_table_insert (self->theme_resources, theme_resource->key, theme_resource);

  if (!self->registered_signals)
    {
      self->registered_signals = TRUE;
      g_signal_connect_object (gtk_settings_get_default (),
                               "notify::gtk-application-prefer-dark-theme",
                               G_CALLBACK (sysprof_theme_manager_queue_reload),
                               self,
                               G_CONNECT_SWAPPED);
      g_signal_connect_object (gtk_settings_get_default (),
                               "notify::gtk-theme-name",
                               G_CALLBACK (sysprof_theme_manager_queue_reload),
                               self,
                               G_CONNECT_SWAPPED);
    }

  sysprof_theme_manager_queue_reload (self);

  return id;
}

void
sysprof_theme_manager_unregister (SysprofThemeManager *self,
                             guint           registration_id)
{
  GHashTableIter iter;
  ThemeResource *theme_resource;

  g_return_if_fail (SYSPROF_IS_THEME_MANAGER (self));

  g_hash_table_iter_init (&iter, self->theme_resources);

  while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&theme_resource))
    {
      if (theme_resource->id == registration_id)
        {
          /* Provider is unregistered during destroy */
          g_hash_table_iter_remove (&iter);
          break;
        }
    }
}
