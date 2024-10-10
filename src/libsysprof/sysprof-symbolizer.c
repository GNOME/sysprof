/* sysprof-symbolizer.c
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

#include "sysprof-symbolizer-private.h"

G_DEFINE_ABSTRACT_TYPE (SysprofSymbolizer, sysprof_symbolizer, G_TYPE_OBJECT)

static void
sysprof_symbolizer_real_prepare_async (SysprofSymbolizer   *self,
                                       SysprofDocument     *document,
                                       GCancellable        *cancellable,
                                       GAsyncReadyCallback  callback,
                                       gpointer             user_data)
{
  g_autoptr(GTask) task = g_task_new (self, cancellable, callback, user_data);
  g_task_return_boolean (task, TRUE);
}

static gboolean
sysprof_symbolizer_real_prepare_finish (SysprofSymbolizer  *self,
                                        GAsyncResult       *result,
                                        GError            **error)
{
  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
sysprof_symbolizer_finalize (GObject *object)
{
  G_OBJECT_CLASS (sysprof_symbolizer_parent_class)->finalize (object);
}

static void
sysprof_symbolizer_class_init (SysprofSymbolizerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_symbolizer_finalize;

  klass->prepare_async = sysprof_symbolizer_real_prepare_async;
  klass->prepare_finish = sysprof_symbolizer_real_prepare_finish;
}

static void
sysprof_symbolizer_init (SysprofSymbolizer *self)
{
}

void
_sysprof_symbolizer_prepare_async (SysprofSymbolizer   *self,
                                   SysprofDocument     *document,
                                   GCancellable        *cancellable,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data)
{
  g_return_if_fail (SYSPROF_IS_SYMBOLIZER (self));
  g_return_if_fail (SYSPROF_IS_DOCUMENT (document));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  SYSPROF_SYMBOLIZER_GET_CLASS (self)->prepare_async (self, document, cancellable, callback, user_data);
}

gboolean
_sysprof_symbolizer_prepare_finish (SysprofSymbolizer  *self,
                                    GAsyncResult       *result,
                                    GError            **error)
{
  g_return_val_if_fail (SYSPROF_IS_SYMBOLIZER (self), FALSE);
  g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

  return SYSPROF_SYMBOLIZER_GET_CLASS (self)->prepare_finish (self, result, error);
}

SysprofSymbol *
_sysprof_symbolizer_symbolize (SysprofSymbolizer        *self,
                               SysprofStrings           *strings,
                               const SysprofProcessInfo *process_info,
                               SysprofAddressContext     context,
                               SysprofAddress            address)
{
  return SYSPROF_SYMBOLIZER_GET_CLASS (self)->symbolize (self, strings, process_info, context, address);
}

void
_sysprof_symbolizer_setup (SysprofSymbolizer     *self,
                           SysprofDocumentLoader *loader)
{
  g_return_if_fail (SYSPROF_IS_SYMBOLIZER (self));
  g_return_if_fail (SYSPROF_IS_DOCUMENT_LOADER (loader));

  if (SYSPROF_SYMBOLIZER_GET_CLASS (self)->setup)
    SYSPROF_SYMBOLIZER_GET_CLASS (self)->setup (self, loader);
}
