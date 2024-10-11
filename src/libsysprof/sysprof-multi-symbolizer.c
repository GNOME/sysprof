/* sysprof-multi-symbolizer.c
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

#include "sysprof-multi-symbolizer.h"
#include "sysprof-symbolizer-private.h"

struct _SysprofMultiSymbolizer
{
  SysprofSymbolizer parent_instance;
  GPtrArray *symbolizers;
  guint frozen : 1;
};

struct _SysprofMultiSymbolizerClass
{
  SysprofSymbolizerClass parent_class;
};

G_DEFINE_FINAL_TYPE (SysprofMultiSymbolizer, sysprof_multi_symbolizer, SYSPROF_TYPE_SYMBOLIZER)

static void
sysprof_multi_symbolizer_prepare_cb (GObject      *object,
                                     GAsyncResult *result,
                                     gpointer      user_data)
{
  SysprofSymbolizer *symbolizer = (SysprofSymbolizer *)object;
  g_autoptr(GTask) task = user_data;
  g_autoptr(GError) error = NULL;
  GPtrArray *state;

  g_assert (SYSPROF_IS_SYMBOLIZER (symbolizer));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  state = g_task_get_task_data (task);

  g_assert (state != NULL);
  g_assert (state->len > 0);

  if (!_sysprof_symbolizer_prepare_finish (symbolizer, result, &error))
    g_warning ("Failed to initialize symbolizer: %s", error->message);

  g_ptr_array_remove (state, symbolizer);

  if (state->len == 0)
    g_task_return_boolean (task, TRUE);
}

static void
sysprof_multi_symbolizer_prepare_async (SysprofSymbolizer   *symbolizer,
                                        SysprofDocument     *document,
                                        GCancellable        *cancellable,
                                        GAsyncReadyCallback  callback,
                                        gpointer             user_data)
{
  SysprofMultiSymbolizer *self = (SysprofMultiSymbolizer *)symbolizer;
  g_autoptr(GTask) task = NULL;
  g_autoptr(GPtrArray) state = NULL;

  g_assert (SYSPROF_IS_MULTI_SYMBOLIZER (self));
  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  state = g_ptr_array_new_with_free_func (g_object_unref);
  for (guint i = 0; i < self->symbolizers->len; i++)
    g_ptr_array_add (state, g_object_ref (g_ptr_array_index (self->symbolizers, i)));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_multi_symbolizer_prepare_async);
  g_task_set_task_data (task, g_ptr_array_ref (state), (GDestroyNotify)g_ptr_array_unref);

  if (state->len == 0)
    {
      g_task_return_boolean (task, TRUE);
      return;
    }

  for (guint i = 0; i < state->len; i++)
    {
      SysprofSymbolizer *child = g_ptr_array_index (state, i);

      _sysprof_symbolizer_prepare_async (child,
                                         document,
                                         cancellable,
                                         sysprof_multi_symbolizer_prepare_cb,
                                         g_object_ref (task));
    }
}

static gboolean
sysprof_multi_symbolizer_prepare_finish (SysprofSymbolizer  *symbolizer,
                                         GAsyncResult       *result,
                                         GError            **error)
{
  g_assert (SYSPROF_IS_MULTI_SYMBOLIZER (symbolizer));
  g_assert (G_IS_TASK (result));
  g_assert (g_task_is_valid (result, symbolizer));

  return g_task_propagate_boolean (G_TASK (result), error);
}

static SysprofSymbol *
sysprof_multi_symbolizer_symbolize (SysprofSymbolizer        *symbolizer,
                                    SysprofStrings           *strings,
                                    const SysprofProcessInfo *process_info,
                                    SysprofAddressContext     context,
                                    SysprofAddress            address)
{
  SysprofMultiSymbolizer *self = SYSPROF_MULTI_SYMBOLIZER (symbolizer);

  for (guint i = 0; i < self->symbolizers->len; i++)
    {
      SysprofSymbolizer *child = g_ptr_array_index (self->symbolizers, i);
      SysprofSymbol *symbol = _sysprof_symbolizer_symbolize (child, strings, process_info, context, address);

      if (symbol != NULL)
        return symbol;
    }

  return NULL;
}

static void
sysprof_multi_symbolizer_setup (SysprofSymbolizer     *symbolizer,
                                SysprofDocumentLoader *loader)
{
  SysprofMultiSymbolizer *self = (SysprofMultiSymbolizer *)symbolizer;

  g_assert (SYSPROF_IS_MULTI_SYMBOLIZER (self));
  g_assert (SYSPROF_IS_DOCUMENT_LOADER (loader));

  self->frozen = TRUE;

  for (guint i = 0; i < self->symbolizers->len; i++)
    {
      SysprofSymbolizer *child = g_ptr_array_index (self->symbolizers, i);

      _sysprof_symbolizer_setup (child, loader);
    }
}

static void
sysprof_multi_symbolizer_finalize (GObject *object)
{
  SysprofMultiSymbolizer *self = (SysprofMultiSymbolizer *)object;

  g_clear_pointer (&self->symbolizers, g_ptr_array_unref);

  G_OBJECT_CLASS (sysprof_multi_symbolizer_parent_class)->finalize (object);
}

static void
sysprof_multi_symbolizer_class_init (SysprofMultiSymbolizerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofSymbolizerClass *symbolizer_class = SYSPROF_SYMBOLIZER_CLASS (klass);

  object_class->finalize = sysprof_multi_symbolizer_finalize;

  symbolizer_class->prepare_async = sysprof_multi_symbolizer_prepare_async;
  symbolizer_class->prepare_finish = sysprof_multi_symbolizer_prepare_finish;
  symbolizer_class->symbolize = sysprof_multi_symbolizer_symbolize;
  symbolizer_class->setup = sysprof_multi_symbolizer_setup;
}

static void
sysprof_multi_symbolizer_init (SysprofMultiSymbolizer *self)
{
  self->symbolizers = g_ptr_array_new_with_free_func (g_object_unref);
}

SysprofMultiSymbolizer *
sysprof_multi_symbolizer_new (void)
{
  return g_object_new (SYSPROF_TYPE_MULTI_SYMBOLIZER, NULL);
}

/**
 * sysprof_multi_symbolizer_add:
 * @self: a #SysprofMultiSymbolizer
 * @symbolizer: (transfer full): a #SysprofSymbolizer
 *
 * Takes ownership of @symbolizer and adds it to the list of symbolizers
 * that will be queried when @self is queried for symbols.
 */
void
sysprof_multi_symbolizer_take (SysprofMultiSymbolizer *self,
                               SysprofSymbolizer      *symbolizer)
{
  g_return_if_fail (SYSPROF_IS_MULTI_SYMBOLIZER (self));
  g_return_if_fail (SYSPROF_IS_SYMBOLIZER (symbolizer));
  g_return_if_fail ((gpointer)self != (gpointer)symbolizer);
  g_return_if_fail (self->frozen == FALSE);

  g_ptr_array_add (self->symbolizers, symbolizer);
}
