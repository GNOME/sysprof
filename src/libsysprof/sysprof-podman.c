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

#include "sysprof-podman.h"

static const char *debug_dirs[] = {
  "/usr/lib/debug",
  "/usr/lib32/debug",
  "/usr/lib64/debug",
};

void
_sysprof_podman_debug_dirs (GPtrArray *dirs)
{
  g_autofree gchar *base_path = NULL;
  g_autoptr(GDir) dir = NULL;
  const gchar *name;

  g_assert (dirs != NULL);

  base_path = g_build_filename (g_get_user_data_dir (),
                                "containers",
                                "storage",
                                "overlay",
                                NULL);

  if (!(dir = g_dir_open (base_path, 0, NULL)))
    return;

  while ((name = g_dir_read_name (dir)))
    {
      for (guint i = 0; i < G_N_ELEMENTS (debug_dirs); i++)
        {
          g_autofree gchar *debug_path = g_build_filename (base_path, name, "diff", debug_dirs[i], NULL);
          if (g_file_test (debug_path, G_FILE_TEST_IS_DIR))
            g_ptr_array_add (dirs, g_steal_pointer (&debug_path));
        }
    }
}

gchar **
sysprof_podman_debug_dirs (void)
{
  GPtrArray *dirs = g_ptr_array_new ();
  _sysprof_podman_debug_dirs (dirs);
  g_ptr_array_add (dirs, NULL);
  return (gchar **)g_ptr_array_free (dirs, FALSE);
}

struct _SysprofPodman
{
  JsonParser *containers_parser;
  JsonParser *layers_parser;
};

void
sysprof_podman_free (SysprofPodman *self)
{
  g_clear_object (&self->containers_parser);
  g_clear_object (&self->layers_parser);
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

SysprofPodman *
sysprof_podman_snapshot_current_user (void)
{
  SysprofPodman *self;

  self = g_slice_new0 (SysprofPodman);
  self->containers_parser = json_parser_new ();
  self->layers_parser = json_parser_new ();

  load_containers (self);
  load_layers (self);

  return self;
}

static const char *
find_parent_layer (JsonParser *parser,
                   const char *layer,
                   GPtrArray  *seen)
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

      /* Avoid cycles by checking if we've seen this parent */
      for (guint j = 0; j < seen->len; j++)
        {
          if (strcmp (parent, g_ptr_array_index (seen, j)) == 0)
            return NULL;
        }

      return parent;
    }

  return NULL;
}

gchar **
sysprof_podman_get_layers (SysprofPodman *self,
                           const char    *container)
{
  const char *layer = NULL;
  GPtrArray *layers;
  JsonNode *root;
  JsonArray *ar;
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
      break;
    }

  /* Now we need to try to locate the layer and all of the parents
   * within the layers.json so that we populate from the most recent
   * layer to those beneath it.
   */
  layers = g_ptr_array_new ();
  g_ptr_array_add (layers, g_strdup (layer));
  while ((layer = find_parent_layer (self->layers_parser, layer, layers)))
    g_ptr_array_add (layers, g_strdup (layer));
  g_ptr_array_add (layers, NULL);

  return (char **)g_ptr_array_free (layers, FALSE);
}
