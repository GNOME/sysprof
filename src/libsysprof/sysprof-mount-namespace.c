/* sysprof-mount-namespace.c
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

#include <gio/gio.h>

#include "timsort/gtktimsortprivate.h"

#include "sysprof-mount-namespace-private.h"

struct _SysprofMountNamespace
{
  GObject    parent_instance;
  GPtrArray *devices;
  GPtrArray *mounts;
  guint      mounts_dirty : 1;
};

static GType
sysprof_mount_namespace_get_item_type (GListModel *model)
{
  return SYSPROF_TYPE_MOUNT;
}

static guint
sysprof_mount_namespace_get_n_items (GListModel *model)
{
  return SYSPROF_MOUNT_NAMESPACE (model)->mounts->len;
}

static gpointer
sysprof_mount_namespace_get_item (GListModel *model,
                                  guint       position)
{
  SysprofMountNamespace *self = SYSPROF_MOUNT_NAMESPACE (model);

  if (position < self->mounts->len)
    return g_object_ref (g_ptr_array_index (self->mounts, position));

  return NULL;
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = sysprof_mount_namespace_get_item_type;
  iface->get_item = sysprof_mount_namespace_get_item;
  iface->get_n_items = sysprof_mount_namespace_get_n_items;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofMountNamespace, sysprof_mount_namespace, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

static void
sysprof_mount_namespace_finalize (GObject *object)
{
  SysprofMountNamespace *self = (SysprofMountNamespace *)object;

  g_clear_pointer (&self->devices, g_ptr_array_unref);
  g_clear_pointer (&self->mounts, g_ptr_array_unref);

  G_OBJECT_CLASS (sysprof_mount_namespace_parent_class)->finalize (object);
}

static void
sysprof_mount_namespace_class_init (SysprofMountNamespaceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_mount_namespace_finalize;
}

static void
sysprof_mount_namespace_init (SysprofMountNamespace *self)
{
  self->devices = g_ptr_array_new_with_free_func (g_object_unref);
  self->mounts = g_ptr_array_new_with_free_func (g_object_unref);
}

SysprofMountNamespace *
sysprof_mount_namespace_new (void)
{
  return g_object_new (SYSPROF_TYPE_MOUNT_NAMESPACE, NULL);
}

SysprofMountNamespace *
sysprof_mount_namespace_copy (SysprofMountNamespace *self)
{
  SysprofMountNamespace *copy;

  g_return_val_if_fail (SYSPROF_IS_MOUNT_NAMESPACE (self), NULL);

  copy = sysprof_mount_namespace_new ();

  for (guint i = 0; i < self->devices->len; i++)
    sysprof_mount_namespace_add_device (copy, g_object_ref (g_ptr_array_index (self->devices, i)));

  return copy;
}

/**
 * sysprof_mount_namespace_add_device:
 * @self: a #SysprofMountNamespace
 * @device: (transfer full): a #SysprofMountDevice
 *
 * Adds information about where a device is mounted on the host for resolving
 * paths to binaries.
 */
void
sysprof_mount_namespace_add_device (SysprofMountNamespace *self,
                                    SysprofMountDevice    *device)
{
  g_return_if_fail (SYSPROF_IS_MOUNT_NAMESPACE (self));
  g_return_if_fail (SYSPROF_IS_MOUNT_DEVICE (device));

  g_ptr_array_add (self->devices, device);
}

/**
 * sysprof_mount_namespace_add_mount:
 * @self: a #SysprofMountNamespace
 * @mount: (transfer full): a #SysprofMount
 *
 */
void
sysprof_mount_namespace_add_mount (SysprofMountNamespace *self,
                                   SysprofMount          *mount)
{
  g_return_if_fail (SYSPROF_IS_MOUNT_NAMESPACE (self));
  g_return_if_fail (SYSPROF_IS_MOUNT (mount));

  g_ptr_array_add (self->mounts, mount);

  self->mounts_dirty = TRUE;
}

static gboolean
is_flatpak_commit (const char *commit)
{
  gsize len;

  if (commit == NULL || (len = strlen (commit)) != 64)
    return FALSE;

  for (gsize i = 0; i < len; i++)
    {
      if (!g_ascii_isxdigit (commit[i]))
        return FALSE;
    }

  return TRUE;
}

static gboolean
path_is_equal_or_below (const char *path,
                        const char *prefix)
{
  gsize prefix_len;

  g_assert (path != NULL);
  g_assert (prefix != NULL);

  prefix_len = strlen (prefix);

  return g_str_has_prefix (path, prefix) &&
         (path[prefix_len] == 0 || path[prefix_len] == '/');
}

static char *
get_ostree_path_alias (const char *path)
{
  static const struct {
    const char *path;
    const char *var_path;
  } aliases[] = {
    { "/home", "/var/home" },
    { "/mnt",  "/var/mnt" },
    { "/opt",  "/var/opt" },
    { "/srv",  "/var/srv" },
  };

  if (path == NULL)
    return NULL;

  /* OSTree systems conventionally redirect these top-level paths into /var.
   * Keep both lexical forms because realpath() would resolve them against the
   * machine analyzing the capture instead of the machine that recorded it.
   */
  for (guint i = 0; i < G_N_ELEMENTS (aliases); i++)
    {
      if (path_is_equal_or_below (path, aliases[i].path))
        return g_strconcat (aliases[i].var_path,
                            path + strlen (aliases[i].path),
                            NULL);

      if (path_is_equal_or_below (path, aliases[i].var_path))
        return g_strconcat (aliases[i].path,
                            path + strlen (aliases[i].var_path),
                            NULL);
    }

  return NULL;
}

static char *
get_flatpak_installation (const char *deployment_path)
{
  const char *app;
  const char *runtime;
  const char *suffix;

  if (deployment_path == NULL)
    return NULL;

  app = g_strrstr (deployment_path, "/app/");
  runtime = g_strrstr (deployment_path, "/runtime/");

  if (app != NULL && runtime != NULL)
    suffix = app > runtime ? app : runtime;
  else
    suffix = app != NULL ? app : runtime;

  if (suffix == NULL || suffix == deployment_path)
    return NULL;

  return g_strndup (deployment_path, suffix - deployment_path);
}

static void
add_installation_once (GPtrArray *installations,
                       char      *installation)
{
  g_assert (installations != NULL);

  if (installation == NULL || !g_path_is_absolute (installation))
    {
      g_free (installation);
      return;
    }

  for (guint i = 0; i < installations->len; i++)
    {
      const char *element = g_ptr_array_index (installations, i);

      if (g_str_equal (element, installation))
        {
          g_free (installation);
          return;
        }
    }

  g_ptr_array_add (installations, installation);
}

static void
add_installation (GPtrArray *installations,
                  char      *installation)
{
  g_autofree char *alias = get_ostree_path_alias (installation);

  g_assert (installations != NULL);

  add_installation_once (installations, installation);
  add_installation_once (installations, g_steal_pointer (&alias));
}

static void
add_flatpak_mount_once (SysprofMountNamespace *self,
                        SysprofStrings        *strings,
                        GHashTable            *seen,
                        const char            *mount_point,
                        const char            *host_path,
                        guint                 *layer)
{
  g_autofree char *key = NULL;

  g_assert (SYSPROF_IS_MOUNT_NAMESPACE (self));
  g_assert (strings != NULL);
  g_assert (seen != NULL);
  g_assert (mount_point != NULL);
  g_assert (layer != NULL);

  if (host_path == NULL || !g_path_is_absolute (host_path))
    return;

  key = g_strconcat (mount_point, "\n", host_path, NULL);

  if (!g_hash_table_add (seen, g_steal_pointer (&key)))
    return;

  sysprof_mount_namespace_add_mount (self,
                                     _sysprof_mount_new_for_overlay (strings,
                                                                     mount_point,
                                                                     host_path,
                                                                     (*layer)++));
}

static void
add_flatpak_mount (SysprofMountNamespace *self,
                   SysprofStrings        *strings,
                   GHashTable            *seen,
                   const char            *mount_point,
                   const char            *host_path,
                   guint                 *layer)
{
  g_autofree char *alias = get_ostree_path_alias (host_path);

  g_assert (SYSPROF_IS_MOUNT_NAMESPACE (self));
  g_assert (strings != NULL);
  g_assert (seen != NULL);
  g_assert (mount_point != NULL);
  g_assert (layer != NULL);

  add_flatpak_mount_once (self, strings, seen, mount_point, host_path, layer);
  add_flatpak_mount_once (self, strings, seen, mount_point, alias, layer);
}

static char *
get_flatpak_extension_commit (const char *extensions,
                              const char *extension_id)
{
  g_auto(GStrv) entries = NULL;

  g_assert (extension_id != NULL);

  if (extensions == NULL)
    return NULL;

  entries = g_strsplit (extensions, ";", 0);

  for (guint i = 0; entries[i]; i++)
    {
      const char *equals = strchr (entries[i], '=');

      if (equals != NULL &&
          (gsize)(equals - entries[i]) == strlen (extension_id) &&
          strncmp (entries[i], extension_id, equals - entries[i]) == 0)
        return g_strdup (equals + 1);
    }

  return NULL;
}

static char *
get_flatpak_debug_id (const char *runtime_id)
{
  static const char platform_suffix[] = ".Platform";
  static const char sdk_suffix[] = ".Sdk";
  gsize len;

  g_assert (runtime_id != NULL);

  len = strlen (runtime_id);

  if (g_str_has_suffix (runtime_id, platform_suffix))
    return g_strdup_printf ("%.*s.Sdk.Debug",
                            (int)(len - strlen (platform_suffix)),
                            runtime_id);

  if (g_str_has_suffix (runtime_id, sdk_suffix))
    return g_strconcat (runtime_id, ".Debug", NULL);

  return NULL;
}

static void
add_flatpak_deployments (SysprofMountNamespace *self,
                         SysprofStrings        *strings,
                         GHashTable            *seen,
                         GPtrArray             *installations,
                         const char            *kind,
                         const char            *id,
                         const char            *arch,
                         const char            *branch,
                         const char            *commit,
                         const char            *mount_point,
                         guint                 *layer)
{
  g_assert (SYSPROF_IS_MOUNT_NAMESPACE (self));
  g_assert (strings != NULL);
  g_assert (seen != NULL);
  g_assert (installations != NULL);
  g_assert (kind != NULL);
  g_assert (id != NULL);
  g_assert (arch != NULL);
  g_assert (branch != NULL);
  g_assert (mount_point != NULL);
  g_assert (layer != NULL);

  if (is_flatpak_commit (commit))
    {
      for (guint i = 0; i < installations->len; i++)
        {
          const char *installation = g_ptr_array_index (installations, i);
          g_autofree char *commit_path = NULL;

          commit_path = g_build_filename (installation,
                                          kind,
                                          id,
                                          arch,
                                          branch,
                                          commit,
                                          "files",
                                          NULL);
          add_flatpak_mount (self, strings, seen, mount_point, commit_path, layer);
        }
    }

  for (guint i = 0; i < installations->len; i++)
    {
      const char *installation = g_ptr_array_index (installations, i);
      g_autofree char *active_path = NULL;

      active_path = g_build_filename (installation,
                                      kind,
                                      id,
                                      arch,
                                      branch,
                                      "active",
                                      "files",
                                      NULL);
      add_flatpak_mount (self, strings, seen, mount_point, active_path, layer);
    }
}

void
sysprof_mount_namespace_add_flatpak (SysprofMountNamespace *self,
                                     SysprofStrings        *strings,
                                     const char            *contents,
                                     gsize                  contents_len)
{
  g_autoptr(GHashTable) seen = NULL;
  g_autoptr(GKeyFile) key_file = NULL;
  g_autoptr(GPtrArray) installations = NULL;
  g_autofree char *app_branch = NULL;
  g_autofree char *app_commit = NULL;
  g_autofree char *app_extensions = NULL;
  g_autofree char *app_id = NULL;
  g_autofree char *app_path = NULL;
  g_autofree char *arch = NULL;
  g_autofree char *debug_commit = NULL;
  g_autofree char *debug_id = NULL;
  g_autofree char *runtime_commit = NULL;
  g_autofree char *runtime_extensions = NULL;
  g_autofree char *runtime_path = NULL;
  g_autofree char *runtime_ref = NULL;
  g_auto(GStrv) runtime_parts = NULL;
  const char *group;
  const char *runtime_arch;
  const char *runtime_branch;
  const char *runtime_id;
  guint layer = 0;
  guint n_runtime_parts;
  guint runtime_part_offset;

  g_return_if_fail (SYSPROF_IS_MOUNT_NAMESPACE (self));
  g_return_if_fail (strings != NULL);
  g_return_if_fail (contents != NULL || contents_len == 0);

  if (contents_len == 0)
    return;

  key_file = g_key_file_new ();

  if (!g_key_file_load_from_data (key_file, contents, contents_len, G_KEY_FILE_NONE, NULL))
    return;

  if (g_key_file_has_group (key_file, "Application"))
    group = "Application";
  else if (g_key_file_has_group (key_file, "Runtime"))
    group = "Runtime";
  else
    return;

  runtime_ref = g_key_file_get_string (key_file, group, "runtime", NULL);

  if (runtime_ref == NULL)
    return;

  runtime_parts = g_strsplit (runtime_ref, "/", 0);

  n_runtime_parts = g_strv_length (runtime_parts);

  /* Flatpak metadata normally omits the ref kind, but captures in the wild
   * also contain the full runtime/ID/ARCH/BRANCH form.
   */
  if (n_runtime_parts == 3)
    runtime_part_offset = 0;
  else if (n_runtime_parts == 4 && g_str_equal (runtime_parts[0], "runtime"))
    runtime_part_offset = 1;
  else
    return;

  runtime_id = runtime_parts[runtime_part_offset];
  runtime_arch = runtime_parts[runtime_part_offset + 1];
  runtime_branch = runtime_parts[runtime_part_offset + 2];

  if (runtime_id[0] == 0 || runtime_arch[0] == 0 || runtime_branch[0] == 0)
    return;

  app_id = g_key_file_get_string (key_file, group, "name", NULL);
  app_path = g_key_file_get_string (key_file, "Instance", "app-path", NULL);
  arch = g_key_file_get_string (key_file, "Instance", "arch", NULL);
  app_branch = g_key_file_get_string (key_file, "Instance", "branch", NULL);
  app_commit = g_key_file_get_string (key_file, "Instance", "app-commit", NULL);
  app_extensions = g_key_file_get_string (key_file, "Instance", "app-extensions", NULL);
  runtime_path = g_key_file_get_string (key_file, "Instance", "runtime-path", NULL);
  runtime_commit = g_key_file_get_string (key_file, "Instance", "runtime-commit", NULL);
  runtime_extensions = g_key_file_get_string (key_file, "Instance", "runtime-extensions", NULL);

  if (arch == NULL)
    arch = g_strdup (runtime_arch);

  /* Deployment paths in .flatpak-info are host paths, so they also give us
   * the non-default installation containing the app or runtime. Search the
   * standard installations too because extensions can be installed in a
   * different installation than the app which caused them to be mounted.
   */
  installations = g_ptr_array_new_with_free_func (g_free);
  add_installation (installations, get_flatpak_installation (runtime_path));
  add_installation (installations, get_flatpak_installation (app_path));

  if (g_get_home_dir () != NULL)
    add_installation (installations,
                      g_build_filename (g_get_home_dir (), ".local", "share", "flatpak", NULL));

  add_installation (installations, g_strdup ("/var/lib/flatpak"));

  seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  add_flatpak_mount (self, strings, seen, "/usr", runtime_path, &layer);
  add_flatpak_deployments (self,
                           strings,
                           seen,
                           installations,
                           "runtime",
                           runtime_id,
                           runtime_arch,
                           runtime_branch,
                           runtime_commit,
                           "/usr",
                           &layer);

  add_flatpak_mount (self, strings, seen, "/app", app_path, &layer);

  /* Application debug extensions use the same split-debug directory layout
   * as SDK debug extensions, rooted at /app/lib/debug instead of
   * /usr/lib/debug.
   */
  if (app_id != NULL && app_branch != NULL)
    {
      g_autofree char *app_debug_commit = NULL;
      g_autofree char *app_debug_id = g_strconcat (app_id, ".Debug", NULL);

      add_flatpak_deployments (self,
                               strings,
                               seen,
                               installations,
                               "app",
                               app_id,
                               arch,
                               app_branch,
                               app_commit,
                               "/app",
                               &layer);

      app_debug_commit = get_flatpak_extension_commit (app_extensions, app_debug_id);
      add_flatpak_deployments (self,
                               strings,
                               seen,
                               installations,
                               "runtime",
                               app_debug_id,
                               arch,
                               app_branch,
                               app_debug_commit,
                               "/app/lib/debug",
                               &layer);
    }

  if (!(debug_id = get_flatpak_debug_id (runtime_id)))
    return;

  /* SDK sandboxes list their Debug extension and exact commit in
   * runtime-extensions. Platform sandboxes do not mount or list it, so infer
   * the corresponding SDK.Debug ID and try the installed active deployment.
   * The ELF loader validates matching build IDs when present, which prevents
   * an out-of-sync active extension from supplying bad names.
   */
  debug_commit = get_flatpak_extension_commit (runtime_extensions, debug_id);
  add_flatpak_deployments (self,
                           strings,
                           seen,
                           installations,
                           "runtime",
                           debug_id,
                           runtime_arch,
                           runtime_branch,
                           debug_commit,
                           "/usr/lib/debug",
                           &layer);
}

static const char *
get_root_relative_to_subvolume (const char *root,
                                const char *subvolume)
{
  g_assert (root != NULL);

  if (subvolume == NULL || subvolume[0] == 0 || g_str_equal (subvolume, "/"))
    return root;

  if (g_str_equal (root, subvolume))
    return "/";

  if (path_is_equal_or_below (root, subvolume))
    return root + strlen (subvolume);

  return NULL;
}

static void
append_unique_path (GArray *strv,
                    char   *path)
{
  g_assert (strv != NULL);
  g_assert (path != NULL);

  for (guint i = 0; i < strv->len; i++)
    {
      const char *element = g_array_index (strv, char *, i);

      if (g_str_equal (element, path))
        {
          g_free (path);
          return;
        }
    }

  g_array_append_val (strv, path);
}

static int
compare_mount (gconstpointer a,
               gconstpointer b)
{
  SysprofMount *mount_a = *(SysprofMount * const *)a;
  SysprofMount *mount_b = *(SysprofMount * const *)b;
  gsize alen = strlen (sysprof_mount_get_mount_point (mount_a));
  gsize blen = strlen (sysprof_mount_get_mount_point (mount_b));

  if (mount_a->is_overlay && !mount_b->is_overlay)
    return -1;
  else if (!mount_a->is_overlay && mount_b->is_overlay)
    return 1;

  if (alen > blen)
    return -1;
  else if (blen > alen)
    return 1;

  if (mount_a->layer < mount_b->layer)
    return -1;
  else if (mount_a->layer > mount_b->layer)
    return 1;

  return 0;
}

/**
 * sysprof_mount_namespace_translate:
 * @self: a #SysprofMountNamespace
 * @file: the path within the mount namespace to translate
 *
 * Attempts to translate a path within the mount namespace into a
 * path available in our current mount namespace.
 *
 * As overlays are involved, multiple paths may be returned which
 * could contain the target file. You should check these starting
 * from the first element in the resulting array to the last.
 *
 * Returns: (transfer full) (nullable): a UTF-8 encoded string array
 *   if successful; otherwise %NULL and @error is set.
 */
char **
sysprof_mount_namespace_translate (SysprofMountNamespace *self,
                                   const char            *file)
{
  g_autoptr(GArray) strv = NULL;

  g_return_val_if_fail (SYSPROF_IS_MOUNT_NAMESPACE (self), NULL);
  g_return_val_if_fail (file != NULL, NULL);

  if G_UNLIKELY (self->mounts_dirty)
    {
      gtk_tim_sort (self->mounts->pdata,
                    self->mounts->len,
                    sizeof (gpointer),
                    (GCompareDataFunc)compare_mount,
                    NULL);
      self->mounts_dirty = FALSE;
    }

  strv = g_array_new (TRUE, FALSE, sizeof (char *));

  for (guint i = 0; i < self->mounts->len; i++)
    {
      SysprofMount *mount = g_ptr_array_index (self->mounts, i);
      const char *fs_type;
      const char *relative;
      char *translated;

      if (!(relative = _sysprof_mount_get_relative_path (mount, file)))
        continue;

      fs_type = sysprof_mount_get_filesystem_type (mount);

      if (mount->is_overlay)
        {
          translated = g_build_filename (mount->mount_source, relative, NULL);
          append_unique_path (strv, translated);
        }
      else if (g_strcmp0 (fs_type, "overlay") == 0)
        {
          g_autofree char *lowerdir_str = sysprof_mount_get_superblock_option (mount, "lowerdir");
          g_autofree char *upperdir_str = sysprof_mount_get_superblock_option (mount, "upperdir");
          g_auto(GStrv) lowerdirs = lowerdir_str ? g_strsplit (lowerdir_str, ":", 0) : NULL;
          g_auto(GStrv) upperdirs = upperdir_str ? g_strsplit (upperdir_str, ":", 0) : NULL;

          if (upperdirs != NULL)
            {
              for (guint j = 0; upperdirs[j]; j++)
                {
                  translated = g_build_filename (upperdirs[j], relative, NULL);
                  append_unique_path (strv, translated);
                }
            }

          if (lowerdirs != NULL)
            {
              for (guint j = 0; lowerdirs[j]; j++)
                {
                  translated = g_build_filename (lowerdirs[j], relative, NULL);
                  append_unique_path (strv, translated);
                }
            }

          continue;
        }
      else
        {
          const char *mount_source = sysprof_mount_get_mount_source (mount);
          const char *root;

          if (!(root = sysprof_mount_get_root (mount)))
            continue;

          /* A device can be visible through multiple mounts on OSTree systems,
           * including /sysroot and bind mounts for /var or /var/home. Preserve
           * every compatible view and let the ELF build ID select the right
           * file instead of guessing which view is canonical.
           */
          for (guint j = 0; j < self->devices->len; j++)
            {
              SysprofMountDevice *device = g_ptr_array_index (self->devices, j);
              const char *device_mount_point;
              const char *device_subvolume;
              const char *relative_root;

              if (g_strcmp0 (sysprof_mount_device_get_fs_spec (device), mount_source) != 0)
                continue;

              device_mount_point = sysprof_mount_device_get_mount_point (device);
              device_subvolume = sysprof_mount_device_get_subvolume (device);

              if (device_mount_point == NULL ||
                  !(relative_root = get_root_relative_to_subvolume (root,
                                                                    device_subvolume)))
                continue;

              translated = g_build_filename (device_mount_point,
                                             relative_root,
                                             relative,
                                             NULL);
              append_unique_path (strv, translated);
            }
        }
    }

  if (strv->len == 0)
    {
      /* Try to passthrough the path in case we had no matches just to give
       * things a chance to decode. This can happen if we never recorded
       * the contents of /proc/$$/mountinfo.
       */
      char *copy = g_strdup (file);
      g_array_append_val (strv, copy);
    }

  return (char **)g_array_free (g_steal_pointer (&strv), FALSE);
}
