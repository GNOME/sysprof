/*
 * sysprof-document-traceable.c
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

#include "sysprof-document-traceable-private.h"

#include "sysprof-document-frame-private.h"
#include "sysprof-document-mmap.h"
#include "sysprof-elf-loader-private.h"

G_DEFINE_INTERFACE (SysprofDocumentTraceable, sysprof_document_traceable, SYSPROF_TYPE_DOCUMENT_FRAME)

static void
sysprof_document_traceable_default_init (SysprofDocumentTraceableInterface *iface)
{
  /**
   * SysprofDocumentTraceable:stack-depth:
   *
   * The "stack-depth" property contains the number of addresses collected
   * in the backtrace.
   *
   * You may use this value to retrieve the addresses from 0 to ("stack-depth"-1)
   * by calling sysprof_document_traceable_get_stack_address().
   *
   * Since: 45
   */
  g_object_interface_install_property (iface,
                                       g_param_spec_uint ("stack-depth", NULL, NULL,
                                                          0, G_MAXUINT16, 0,
                                                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

  /**
   * SysprofDocumentTraceable:thread-id:
   *
   * The "thread-id" property contains the thread identifier for the traceable.
   *
   * Since: 45
   */
  g_object_interface_install_property (iface,
                                       g_param_spec_int ("thread-id", NULL, NULL,
                                                         -1, G_MAXINT, -1,
                                                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
}

guint
sysprof_document_traceable_get_stack_depth (SysprofDocumentTraceable *self)
{
  return SYSPROF_DOCUMENT_TRACEABLE_GET_IFACE (self)->get_stack_depth (self);
}

guint64
sysprof_document_traceable_get_stack_address (SysprofDocumentTraceable *self,
                                              guint                     position)
{
  return SYSPROF_DOCUMENT_TRACEABLE_GET_IFACE (self)->get_stack_address (self, position);
}

int
sysprof_document_traceable_get_thread_id (SysprofDocumentTraceable *self)
{
  return SYSPROF_DOCUMENT_TRACEABLE_GET_IFACE (self)->get_thread_id (self);
}

guint
sysprof_document_traceable_get_stack_addresses (SysprofDocumentTraceable *self,
                                                guint64                  *addresses,
                                                guint                     n_addresses)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_TRACEABLE (self), 0);

  if (addresses == NULL || n_addresses == 0)
    return 0;

  return SYSPROF_DOCUMENT_TRACEABLE_GET_IFACE (self)->get_stack_addresses (self, addresses, n_addresses);
}

typedef struct
{
  SysprofMountNamespace *mount_namespace;
  char *path;
  char *build_id;
  char **debug_dirs;
  char **external_debug_dirs;
  guint64 file_inode;
  guint64 file_offset;
} ResolveSourceLocation;

static void
resolve_source_location_free (gpointer data)
{
  ResolveSourceLocation *state = data;

  g_clear_object (&state->mount_namespace);
  g_clear_pointer (&state->path, g_free);
  g_clear_pointer (&state->build_id, g_free);
  g_clear_pointer (&state->debug_dirs, g_strfreev);
  g_clear_pointer (&state->external_debug_dirs, g_strfreev);
  g_free (state);
}

static DexFuture *
resolve_source_location_thread (gpointer data)
{
  ResolveSourceLocation *state = data;
  g_autoptr(SysprofElfLoader) loader = sysprof_elf_loader_new ();
  g_autoptr(SysprofElf) elf = NULL;
  g_autoptr(SysprofSourceLocation) location = NULL;

  if (state->debug_dirs != NULL)
    sysprof_elf_loader_set_debug_dirs (loader, (const char * const *)state->debug_dirs);
  if (state->external_debug_dirs != NULL)
    sysprof_elf_loader_set_external_debug_dirs (loader,
                                                (const char * const *)state->external_debug_dirs);

  if ((elf = sysprof_elf_loader_load (loader,
                                     state->mount_namespace,
                                     state->path,
                                     state->build_id,
                                     state->file_inode,
                                     NULL)) != NULL)
    location = sysprof_elf_resolve_source_location (elf,
                                                    state->mount_namespace,
                                                    state->file_offset);

  if (location == NULL)
    return dex_future_new_reject (G_IO_ERROR,
                                  G_IO_ERROR_NOT_FOUND,
                                  "No source location found for stack address");

  return dex_future_new_take_object (g_steal_pointer (&location));
}

DexFuture *
_sysprof_document_traceable_resolve_source_location (SysprofDocumentTraceable *self,
                                                     guint                     position,
                                                     const char * const       *debug_dirs,
                                                     const char * const       *external_debug_dirs)
{
  SysprofProcessInfo *process_info;
  SysprofDocumentMmap *map;
  ResolveSourceLocation *state;
  g_autofree guint64 *addresses = NULL;
  SysprofAddressContext context = SYSPROF_ADDRESS_CONTEXT_NONE;
  SysprofAddressContext next_context;
  guint64 address;
  guint depth;
  gboolean has_previous_frame = FALSE;
  static DexThreadPool *pool;
  static gsize pool_initialized;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_TRACEABLE (self), NULL);

  depth = sysprof_document_traceable_get_stack_depth (self);

  if (position >= depth)
    return dex_future_new_reject (G_IO_ERROR,
                                  G_IO_ERROR_INVALID_ARGUMENT,
                                  "Stack position is out of range");

  addresses = g_new (guint64, depth);
  if (sysprof_document_traceable_get_stack_addresses (self, addresses, depth) != depth)
    return dex_future_new_reject (G_IO_ERROR,
                                  G_IO_ERROR_INVALID_DATA,
                                  "Could not read the stack addresses");

  for (guint i = 0; i <= position; i++)
    {
      if (sysprof_address_is_context_switch (addresses[i], &next_context))
        {
          if (i == position)
            return dex_future_new_reject (G_IO_ERROR,
                                          G_IO_ERROR_NOT_FOUND,
                                          "Stack position is a context switch");
          context = next_context;
          has_previous_frame = FALSE;
        }
      else if (i < position)
        {
          has_previous_frame = TRUE;
        }
    }

  if (context != SYSPROF_ADDRESS_CONTEXT_NONE && context != SYSPROF_ADDRESS_CONTEXT_USER)
    return dex_future_new_reject (G_IO_ERROR,
                                  G_IO_ERROR_NOT_FOUND,
                                  "Stack address is outside user space");

  address = addresses[position];

  if (has_previous_frame)
    {
      if (address == 0)
        return dex_future_new_reject (G_IO_ERROR,
                                      G_IO_ERROR_NOT_FOUND,
                                      "Caller return address is zero");

      address--;
    }

  process_info = SYSPROF_DOCUMENT_FRAME (self)->process_info;

  if (process_info == NULL ||
      process_info->address_layout == NULL ||
      process_info->mount_namespace == NULL ||
      (map = sysprof_address_layout_lookup (process_info->address_layout, address)) == NULL)
    return dex_future_new_reject (G_IO_ERROR,
                                  G_IO_ERROR_NOT_FOUND,
                                  "No ELF mapping found for stack address");

  state = g_new0 (ResolveSourceLocation, 1);
  state->mount_namespace = sysprof_mount_namespace_copy (process_info->mount_namespace);
  state->path = g_strdup (sysprof_document_mmap_get_file (map));
  state->build_id = g_strdup (sysprof_document_mmap_get_build_id (map));
  state->debug_dirs = g_strdupv ((char **)debug_dirs);
  state->external_debug_dirs = g_strdupv ((char **)external_debug_dirs);
  state->file_inode = sysprof_document_mmap_get_file_inode (map);
  state->file_offset = address - sysprof_document_mmap_get_start_address (map) +
                       sysprof_document_mmap_get_file_offset (map);

  if (g_once_init_enter (&pool_initialized))
    {
      pool = dex_thread_pool_new (MIN (4, MAX (1, g_get_num_processors ())));
      g_once_init_leave (&pool_initialized, 1);
    }

  if (pool == NULL)
    {
      resolve_source_location_free (state);
      return dex_future_new_reject (G_IO_ERROR,
                                    G_IO_ERROR_FAILED,
                                    "Could not create source location worker pool");
    }

  return dex_thread_pool_submit (pool,
                                 "sysprof-source-location",
                                 resolve_source_location_thread,
                                 state,
                                 resolve_source_location_free);
}
