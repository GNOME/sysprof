/* sysprof-podman.c
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

#define G_LOG_DOMAIN "sysprof-podman"

#include "config.h"

#include <json-glib/json-glib.h>

#include "sysprof-podman-private.h"

struct _SysprofPodman
{
  JsonParser *containers_parser;
  JsonParser *layers_parser;
  JsonParser *images_parser;
};

void
sysprof_podman_free (SysprofPodman *self)
{
  g_clear_object (&self->containers_parser);
  g_clear_object (&self->layers_parser);
  g_clear_object (&self->images_parser);
  g_slice_free (SysprofPodman, self);
}

static void
load_containers (SysprofPodman *self)
{
  g_autofree char *path = NULL;

  g_assert (self != NULL);

  path = g_build_filename (g_get_user_data_dir (),
                           "containers",
                           "storage",
                           "overlay-containers",
                           "containers.json",
                           NULL);
  json_parser_load_from_file (self->containers_parser, path, NULL);
}

static void
load_layers (SysprofPodman *self)
{
  g_autofree char *path = NULL;

  g_assert (self != NULL);

  path = g_build_filename (g_get_user_data_dir (),
                           "containers",
                           "storage",
                           "overlay-layers",
                           "layers.json",
                           NULL);
  json_parser_load_from_file (self->layers_parser, path, NULL);
}

static void
load_images (SysprofPodman *self)
{
  g_autofree char *path = NULL;

  g_assert (self != NULL);

  path = g_build_filename (g_get_user_data_dir (),
                           "containers",
                           "storage",
                           "overlay-images",
                           "images.json",
                           NULL);
  json_parser_load_from_file (self->images_parser, path, NULL);
}

SysprofPodman *
sysprof_podman_snapshot_current_user (void)
{
  SysprofPodman *self;

  self = g_slice_new0 (SysprofPodman);
  self->containers_parser = json_parser_new ();
  self->layers_parser = json_parser_new ();
  self->images_parser = json_parser_new ();

  load_containers (self);
  load_layers (self);
  load_images (self);

  return self;
}

static const char *
find_image_layer (JsonParser *parser,
                  const char *image)
{
  JsonNode *root;
  JsonArray *ar;
  guint n_items;

  g_assert (JSON_IS_PARSER (parser));
  g_assert (image != NULL);

  if (!(root = json_parser_get_root (parser)) ||
      !JSON_NODE_HOLDS_ARRAY (root) ||
      !(ar = json_node_get_array (root)))
    return NULL;

  n_items = json_array_get_length (ar);

  for (guint i = 0; i < n_items; i++)
    {
      JsonObject *item = json_array_get_object_element (ar, i);
      const char *id;
      const char *layer;

      if (item == NULL ||
          !json_object_has_member (item, "id") ||
          !json_object_has_member (item, "layer") ||
          !(id = json_object_get_string_member (item, "id")) ||
          strcmp (id, image) != 0 ||
          !(layer = json_object_get_string_member (item, "layer")))
        continue;

      return layer;
    }

  return NULL;
}

static const char *
find_parent_layer (JsonParser *parser,
                   const char *layer,
                   GHashTable *seen)
{
  JsonNode *root;
  JsonArray *ar;
  guint n_items;

  g_assert (JSON_IS_PARSER (parser));
  g_assert (layer != NULL);
  g_assert (seen != NULL);

  if (!(root = json_parser_get_root (parser)) ||
      !JSON_NODE_HOLDS_ARRAY (root) ||
      !(ar = json_node_get_array (root)))
    return NULL;

  n_items = json_array_get_length (ar);

  for (guint i = 0; i < n_items; i++)
    {
      JsonObject *item = json_array_get_object_element (ar, i);
      const char *parent;
      const char *id;

      if (item == NULL ||
          !json_object_has_member (item, "id") ||
          !json_object_has_member (item, "parent") ||
          !(id = json_object_get_string_member (item, "id")) ||
          strcmp (id, layer) != 0 ||
          !(parent = json_object_get_string_member (item, "parent")))
        continue;

      if (g_hash_table_contains (seen, parent))
        return NULL;

      return parent;
    }

  return NULL;
}

static char *
get_layer_dir (const char *layer)
{
  /* We don't use XDG data dir because this might be in a container
   * or flatpak environment that doesn't match. And generally, it's
   * always .local.
   */
  return g_build_filename (g_get_home_dir (),
                           ".local",
                           "share",
                           "containers",
                           "storage",
                           "overlay",
                           layer,
                           "diff",
                           NULL);
}

gchar **
sysprof_podman_get_layers (SysprofPodman *self,
                           const char    *container)
{
  const char *layer = NULL;
  const char *image = NULL;
  GHashTable *layers;
  JsonNode *root;
  JsonArray *ar;
  const char **keys;
  char **ret;
  guint n_items;

  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (container != NULL, NULL);

  if (!(root = json_parser_get_root (self->containers_parser)) ||
      !JSON_NODE_HOLDS_ARRAY (root) ||
      !(ar = json_node_get_array (root)))
    return NULL;

  n_items = json_array_get_length (ar);

  /* First try to locate the "layer" identifier for the container
   * in question.
   */
  for (guint i = 0; i < n_items; i++)
    {
      JsonObject *item = json_array_get_object_element (ar, i);
      const char *item_layer;
      const char *id;

      if (item == NULL ||
          !(id = json_object_get_string_member (item, "id")) ||
          strcmp (id, container) != 0 ||
          !(item_layer = json_object_get_string_member (item, "layer")))
        continue;

      layer = item_layer;
      image = json_object_get_string_member (item, "image");
      break;
    }

  layers = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  /* Add all our known layers starting from current layer */
  if (layer != NULL)
    {
      do
        g_hash_table_add (layers, get_layer_dir (layer));
      while ((layer = find_parent_layer (self->layers_parser, layer, layers)));
    }

  /* If an image was specified, add its layer */
  if (image != NULL)
    {
      if ((layer = find_image_layer (self->images_parser, image)))
        {
          do
            g_hash_table_add (layers, get_layer_dir (layer));
          while ((layer = find_parent_layer (self->layers_parser, layer, layers)));
        }
    }

  keys = (const char **)g_hash_table_get_keys_as_array (layers, NULL);
  ret = g_strdupv ((char **)keys);

  g_hash_table_unref (layers);
  g_free (keys);

  return ret;
}
