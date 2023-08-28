/* sysprof-process-info.c
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

#include "sysprof-process-info-private.h"
#include "sysprof-symbol-private.h"

G_DEFINE_BOXED_TYPE (SysprofProcessInfo,
                     sysprof_process_info,
                     sysprof_process_info_ref,
                     sysprof_process_info_unref)

SysprofProcessInfo *
sysprof_process_info_new (SysprofMountNamespace *mount_namespace,
                          int                    pid)
{
  SysprofProcessInfo *self;
  char pidstr[32];
  char symname[32];

  g_snprintf (symname, sizeof symname, "Process %d", pid);
  g_snprintf (pidstr, sizeof pidstr, "(%d)", pid);

  self = g_atomic_rc_box_new0 (SysprofProcessInfo);
  self->pid = pid;
  self->address_layout = sysprof_address_layout_new ();
  self->symbol_cache = sysprof_symbol_cache_new ();
  self->mount_namespace = mount_namespace;
  self->thread_ids = egg_bitset_new_empty ();
  self->fallback_symbol = _sysprof_symbol_new (g_ref_string_new (symname),
                                               NULL,
                                               g_ref_string_new (pidstr),
                                               0, 0,
                                               SYSPROF_SYMBOL_KIND_PROCESS);

  if (pid == 0)
    self->fallback_symbol->is_kernel_process = TRUE;

  if (pid > 0)
    egg_bitset_add (self->thread_ids, pid);

  return self;
}

SysprofProcessInfo *
sysprof_process_info_ref (SysprofProcessInfo *self)
{
  return g_atomic_rc_box_acquire (self);
}

static void
sysprof_process_info_finalize (gpointer data)
{
  SysprofProcessInfo *self = data;

  g_clear_object (&self->address_layout);
  g_clear_object (&self->symbol_cache);
  g_clear_object (&self->mount_namespace);
  g_clear_object (&self->fallback_symbol);
  g_clear_object (&self->shared_symbol);
  g_clear_object (&self->symbol);
  g_clear_pointer (&self->thread_ids, egg_bitset_unref);

  self->pid = 0;
}

void
sysprof_process_info_unref (SysprofProcessInfo *self)
{
  g_atomic_rc_box_release_full (self, sysprof_process_info_finalize);
}
