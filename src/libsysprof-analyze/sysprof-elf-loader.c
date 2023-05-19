/* sysprof-elf-loader.c
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

#include "sysprof-elf-private.h"
#include "sysprof-elf-loader-private.h"
#include "sysprof-strings-private.h"

#define DEFAULT_DEBUG_DIRS SYSPROF_STRV_INIT("/usr/lib/debug")

struct _SysprofElfLoader
{
  GObject parent_instance;
  GHashTable *cache;
  char **debug_dirs;
  char **external_debug_dirs;
};

enum {
  PROP_0,
  PROP_DEBUG_DIRS,
  PROP_EXTERNAL_DEBUG_DIRS,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofElfLoader, sysprof_elf_loader, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];
static gboolean in_flatpak;
static gboolean in_podman;

SysprofElfLoader *
sysprof_elf_loader_new (void)
{
  return g_object_new (SYSPROF_TYPE_ELF_LOADER, NULL);
}

static void
sysprof_elf_loader_finalize (GObject *object)
{
  SysprofElfLoader *self = (SysprofElfLoader *)object;

  g_clear_pointer (&self->debug_dirs, g_strfreev);
  g_clear_pointer (&self->external_debug_dirs, g_strfreev);
  g_clear_pointer (&self->cache, g_hash_table_unref);

  G_OBJECT_CLASS (sysprof_elf_loader_parent_class)->finalize (object);
}

static void
sysprof_elf_loader_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  SysprofElfLoader *self = SYSPROF_ELF_LOADER (object);

  switch (prop_id)
    {
    case PROP_DEBUG_DIRS:
      g_value_set_boxed (value, sysprof_elf_loader_get_debug_dirs (self));
      break;

    case PROP_EXTERNAL_DEBUG_DIRS:
      g_value_set_boxed (value, sysprof_elf_loader_get_external_debug_dirs (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_elf_loader_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  SysprofElfLoader *self = SYSPROF_ELF_LOADER (object);

  switch (prop_id)
    {
    case PROP_DEBUG_DIRS:
      sysprof_elf_loader_set_debug_dirs (self, g_value_get_boxed (value));
      break;

    case PROP_EXTERNAL_DEBUG_DIRS:
      sysprof_elf_loader_set_external_debug_dirs (self, g_value_get_boxed (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_elf_loader_class_init (SysprofElfLoaderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_elf_loader_finalize;
  object_class->get_property = sysprof_elf_loader_get_property;
  object_class->set_property = sysprof_elf_loader_set_property;

  properties [PROP_DEBUG_DIRS] =
    g_param_spec_boxed ("debug-dirs", NULL, NULL,
                        G_TYPE_STRV,
                        (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_EXTERNAL_DEBUG_DIRS] =
    g_param_spec_boxed ("external-debug-dirs", NULL, NULL,
                        G_TYPE_STRV,
                        (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  in_flatpak = g_file_test ("/.flatpak-info", G_FILE_TEST_EXISTS);
  in_podman = g_file_test ("/run/.containerenv", G_FILE_TEST_EXISTS);
}

static void
sysprof_elf_loader_init (SysprofElfLoader *self)
{
  self->debug_dirs = g_strdupv ((char **)DEFAULT_DEBUG_DIRS);
  self->cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
}

/**
 * sysprof_elf_loader_get_debug_dirs:
 * @self: a #SysprofElfLoader
 *
 * Gets the #SysprofElfLoader:debug-dirs property.
 *
 * See sysprof_elf_loader_set_debug_dirs() for information on how
 * these directories are used.
 *
 * Returns: (nullable): an array of debug directories, or %NULL
 */
const char * const *
sysprof_elf_loader_get_debug_dirs (SysprofElfLoader *self)
{
  g_return_val_if_fail (SYSPROF_IS_ELF_LOADER (self), NULL);

  return (const char * const *)self->debug_dirs;
}

/**
 * sysprof_elf_loader_set_debug_dirs:
 * @self: a #SysprofElfLoader
 * @debug_dirs: the new debug directories to use
 *
 * Sets the #SysprofElfLoader:debug-dirs prpoerty.
 *
 * If @debug_dirs is %NULL, the default debug directories will
 * be used.
 *
 * These directories will be used to resolve debug symbols within
 * the mount namespace of the process whose symbols are being
 * resolved.
 *
 * To set a global directory that may contain debug symbols, use
 * sysprof_elf_loader_set_external_debug_dirs() which can be searched
 * regardless of what mount namespace the resolving process was in.
 */
void
sysprof_elf_loader_set_debug_dirs (SysprofElfLoader   *self,
                                   const char * const *debug_dirs)
{
  g_return_if_fail (SYSPROF_IS_ELF_LOADER (self));
  g_return_if_fail (self->debug_dirs != NULL);

  if (sysprof_set_strv (&self->debug_dirs, debug_dirs))
    g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_DEBUG_DIRS]);
}

/**
 * sysprof_elf_loader_get_external_debug_dirs:
 * @self: a #SysprofElfLoader
 *
 * Gets the #SysprofElfLoader:external-debug-dirs property.
 *
 * See sysprof_elf_loader_set_external_debug_dirs() for how this
 * property is used to locate ELF files.
 *
 * Returns: (nullable): an array of external debug directories, or %NULL
 */
const char * const *
sysprof_elf_loader_get_external_debug_dirs (SysprofElfLoader *self)
{
  g_return_val_if_fail (SYSPROF_IS_ELF_LOADER (self), NULL);

  return (const char * const *)self->external_debug_dirs;
}

/**
 * sysprof_elf_loader_set_external_debug_dirs:
 * @self: a #SysprofElfLoader
 * @external_debug_dirs: (nullable): array of debug directories to resolve
 *   `.gnu_debuglink` references in ELF files.
 *
 * Sets the #SysprofElfLoader:external-debug-dirs property.
 *
 * This is used to resolve `.gnu_debuglink` files across any process that is
 * loading ELF files with this #SysprofElfLoader. That allows for symbolizing
 * a capture file that was recording on a different system for which the
 * debug symbols only exist locally.
 *
 * Note that this has no effect on stack trace unwinding, that must occur
 * independently of this, when the samples are recorded by the specific
 * unwinder in use.
 */
void
sysprof_elf_loader_set_external_debug_dirs (SysprofElfLoader   *self,
                                            const char * const *external_debug_dirs)
{
  g_return_if_fail (SYSPROF_IS_ELF_LOADER (self));

  if (sysprof_set_strv (&self->external_debug_dirs, external_debug_dirs))
    g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_EXTERNAL_DEBUG_DIRS]);
}

static void
sysprof_elf_loader_annotate (SysprofElfLoader      *self,
                             SysprofMountNamespace *mount_namespace,
                             SysprofElf            *elf,
                             const char            *debug_link)
{
  g_assert (SYSPROF_IS_ELF_LOADER (self));
  g_assert (SYSPROF_IS_MOUNT_NAMESPACE (mount_namespace));
  g_assert (SYSPROF_IS_ELF (elf));
  g_assert (debug_link != NULL);

  /* TODO: We want to use the debug_link to find a similar file that will
   *       contain the various debugsymbols. We need to look for it in
   *       any of the #SysprofElfLoader:debug-dirs (and translated via the
   *       mount namespace) as well as any of the :external_debug_dirs
   *       which is a path available to us from our application's mount
   *       namespace. We recursively follow those debug_link (and must
   *       protect against cycles) to get the final/best debuglink file.
   *       That will get assigned via sysprof_elf_set_debug_link_elf().
   */

}

/**
 * sysprof_elf_loader_load:
 * @self: a #SysprofElfLoader
 * @mount_namespace: a #SysprofMountNamespace for path resolving
 * @file: the path of the file to load within the mount namespace
 * @build_id: (nullable): an optional build-id that can be used to resolve
 *   the file alternatively to the file path
 *
 * Attempts to load a #SysprofElf for @file (or optionally by @build_id).
 *
 * This attempts to follow `.gnu_debuglink` ELF section headers and attach
 * them to the resulting #SysprofElf so that additional symbol information
 * is available.
 *
 * Returns: (transfer full): a #SysprofElf, or %NULL if the file could
 *   not be resolved.
 */
SysprofElf *
sysprof_elf_loader_load (SysprofElfLoader       *self,
                         SysprofMountNamespace  *mount_namespace,
                         const char             *file,
                         const char             *build_id,
                         GError                **error)
{
  g_auto(GStrv) paths = NULL;

  g_return_val_if_fail (SYSPROF_IS_ELF_LOADER (self), NULL);
  g_return_val_if_fail (SYSPROF_IS_MOUNT_NAMESPACE (mount_namespace), NULL);

  /* We must translate the file into a number of paths that may possibly
   * locate the file in the case that there are overlays in the mount
   * namespace. Each of the paths could be in a lower overlay layer.
   */
  if (!(paths = sysprof_mount_namespace_translate (mount_namespace, file)))
    goto failure;

  for (guint i = 0; paths[i]; i++)
    {
      g_autoptr(GMappedFile) mapped_file = NULL;
      g_autoptr(SysprofElf) elf = NULL;
      g_autoptr(GError) local_error = NULL;
      g_autofree char *container_path = NULL;
      SysprofElf *cached_elf;
      const char *path = paths[i];
      const char *debug_link;

      if ((in_flatpak && !g_str_has_prefix (path, "/home/")) || in_podman)
        path = container_path = g_build_filename ("/var/run/host", path, NULL);

      if ((cached_elf = g_hash_table_lookup (self->cache, path)))
        return g_object_ref (cached_elf);

      if (!(mapped_file = g_mapped_file_new (path, FALSE, NULL)))
        continue;

      if (!(elf = sysprof_elf_new (path, g_steal_pointer (&mapped_file), &local_error)))
        continue;

      if ((debug_link = sysprof_elf_get_debug_link (elf)))
        sysprof_elf_loader_annotate (self, mount_namespace, elf, debug_link);

      g_hash_table_insert (self->cache, g_strdup (path), g_object_ref (elf));

      return g_steal_pointer (&elf);
    }

failure:
  g_set_error_literal (error,
                       G_FILE_ERROR,
                       G_FILE_ERROR_NOENT,
                       "Failed to locate file");

  return NULL;
}
