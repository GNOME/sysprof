/*
 * sysprof-symbol-private.h
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

#pragma once

#include "sysprof-symbol.h"

G_BEGIN_DECLS

struct _SysprofSymbol
{
  GObject parent_instance;

  guint hash;
  guint simple_hash;

  GRefString *name;
  GRefString *binary_path;
  GRefString *binary_nick;

  SysprofAddress begin_address;
  SysprofAddress end_address;

  guint kind : 3;
  guint is_fallback : 1;
  guint is_kernel_process : 1;
};

SysprofSymbol *_sysprof_symbol_new (GRefString       *name,
                                    GRefString       *binary_path,
                                    GRefString       *binary_nick,
                                    SysprofAddress    begin_address,
                                    SysprofAddress    end_address,
                                    SysprofSymbolKind kind);

static inline SysprofSymbol *
_sysprof_symbol_copy (SysprofSymbol *self)
{
  SysprofSymbol *copy;

  copy = _sysprof_symbol_new (self->name ? g_ref_string_acquire (self->name) : NULL,
                              self->binary_path ? g_ref_string_acquire (self->binary_path) : NULL,
                              self->binary_nick ? g_ref_string_acquire (self->binary_nick) : NULL,
                              self->begin_address,
                              self->end_address,
                              self->kind);

  return copy;
}

static inline gboolean
_sysprof_symbol_equal (const SysprofSymbol *a,
                       const SysprofSymbol *b)
{
  if (a == b)
    return TRUE;

  if (a->simple_hash != b->simple_hash)
    return FALSE;

  if (a->kind != b->kind)
    return FALSE;

  if (a->hash != b->hash)
    {
      /* Special case symbols w/o binary_path which can happen
       * when we have symbols decoded from a bundled symbols.
       */
      if (a->binary_path == NULL || b->binary_path == NULL)
        return TRUE;

      return FALSE;
    }

  return strcmp (a->name, b->name) == 0;
}

static inline gboolean
_sysprof_symbol_is_context_switch (SysprofSymbol *symbol)
{
  return symbol->kind == SYSPROF_SYMBOL_KIND_CONTEXT_SWITCH;
}

static inline gboolean
_sysprof_symbol_is_system_library (SysprofSymbol *symbol)
{
  switch (symbol->kind)
    {
    case SYSPROF_SYMBOL_KIND_ROOT:
    case SYSPROF_SYMBOL_KIND_PROCESS:
    case SYSPROF_SYMBOL_KIND_THREAD:
    case SYSPROF_SYMBOL_KIND_CONTEXT_SWITCH:
    case SYSPROF_SYMBOL_KIND_UNWINDABLE:
    default:
      return FALSE;

    case SYSPROF_SYMBOL_KIND_KERNEL:
      return TRUE;

    case SYSPROF_SYMBOL_KIND_USER:
      if (symbol->binary_nick != NULL)
        return TRUE;

      if (symbol->binary_path != NULL)
        return !!strstr (symbol->binary_path, "/usr/");

      return FALSE;
    }
}

G_END_DECLS
