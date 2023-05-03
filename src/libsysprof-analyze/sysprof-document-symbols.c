/* sysprof-document-symbols.c
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

#include "sysprof-document-symbols-private.h"

struct _SysprofDocumentSymbols
{
  GObject            parent_instance;
  SysprofSymbolizer *symbolizer;
};

G_DEFINE_FINAL_TYPE (SysprofDocumentSymbols, sysprof_document_symbols, G_TYPE_OBJECT)

static void
sysprof_document_symbols_finalize (GObject *object)
{
  SysprofDocumentSymbols *self = (SysprofDocumentSymbols *)object;

  g_clear_object (&self->symbolizer);

  G_OBJECT_CLASS (sysprof_document_symbols_parent_class)->finalize (object);
}

static void
sysprof_document_symbols_class_init (SysprofDocumentSymbolsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_document_symbols_finalize;
}

static void
sysprof_document_symbols_init (SysprofDocumentSymbols *self)
{
}

SysprofDocumentSymbols *
_sysprof_document_symbols_new (SysprofDocument   *document,
                               SysprofSymbolizer *symbolizer)
{
  SysprofDocumentSymbols *self;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (document), NULL);
  g_return_val_if_fail (SYSPROF_IS_SYMBOLIZER (symbolizer), NULL);

  self = g_object_new (SYSPROF_TYPE_DOCUMENT_SYMBOLS, NULL);

  /* TODO: async generation of symbols */

  return self;
}
