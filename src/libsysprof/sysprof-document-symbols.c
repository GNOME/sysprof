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

#include <glib/gi18n.h>

#include <libdex.h>

#include "sysprof-address-layout-private.h"
#include "sysprof-document-private.h"
#include "sysprof-document-symbols-private.h"
#include "sysprof-document-traceable.h"
#include "sysprof-mount-namespace-private.h"
#include "sysprof-no-symbolizer.h"
#include "sysprof-symbol-private.h"
#include "sysprof-symbolizer-private.h"

G_DEFINE_FINAL_TYPE (SysprofDocumentSymbols, sysprof_document_symbols, G_TYPE_OBJECT)

static void
sysprof_document_symbols_finalize (GObject *object)
{
  SysprofDocumentSymbols *self = (SysprofDocumentSymbols *)object;

  for (guint i = 0; i < G_N_ELEMENTS (self->context_switches); i++)
    g_clear_object (&self->context_switches[i]);

  g_clear_object (&self->kernel_symbols);

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
  self->kernel_symbols = sysprof_symbol_cache_new ();
}

typedef struct _Symbolize
{
  SysprofDocument        *document;
  SysprofSymbolizer      *symbolizer;
  SysprofDocumentSymbols *symbols;
  SysprofStrings         *strings;
  GHashTable             *pid_to_process_info;
  ProgressFunc            progress_func;
  gpointer                progress_data;
  GDestroyNotify          progress_data_destroy;
  guint                   n_partitions;
  guint                   partition_seq;
  guint                   progress;
} Symbolize;

static void
symbolize_free (Symbolize *state)
{
  if (state->progress_data_destroy)
    state->progress_data_destroy (state->progress_data);
  g_clear_object (&state->document);
  g_clear_object (&state->symbolizer);
  g_clear_object (&state->symbols);
  g_clear_pointer (&state->strings, sysprof_strings_unref);
  g_clear_pointer (&state->pid_to_process_info, g_hash_table_unref);
  g_free (state);
}

static void
add_traceable (SysprofDocumentSymbols   *self,
               SysprofStrings           *strings,
               SysprofProcessInfo       *process_info,
               SysprofDocumentTraceable *traceable,
               SysprofSymbolizer        *symbolizer)
{
  SysprofAddressContext last_context;
  guint64 *addresses;
  guint n_addresses;

  g_assert (SYSPROF_IS_DOCUMENT_TRACEABLE (traceable));
  g_assert (SYSPROF_IS_SYMBOLIZER (symbolizer));

  n_addresses = sysprof_document_traceable_get_stack_depth (traceable);
  addresses = g_alloca (sizeof (guint64) * n_addresses);
  sysprof_document_traceable_get_stack_addresses (traceable, addresses, n_addresses);

  last_context = SYSPROF_ADDRESS_CONTEXT_NONE;

  for (guint i = 0; i < n_addresses; i++)
    {
      SysprofAddress address = addresses[i];
      SysprofAddressContext context = SYSPROF_ADDRESS_CONTEXT_NONE;

      if (sysprof_address_is_context_switch (address, &context))
        {
          last_context = context;
          continue;
        }

      if (last_context == SYSPROF_ADDRESS_CONTEXT_KERNEL)
        {
          g_autoptr(SysprofSymbol) symbol = NULL;

          if (sysprof_symbol_cache_lookup (self->kernel_symbols, address) != NULL)
            continue;

          if ((symbol = _sysprof_symbolizer_symbolize (symbolizer, strings, process_info, last_context, address)))
            sysprof_symbol_cache_take (self->kernel_symbols, g_steal_pointer (&symbol));
        }
      else
        {
          g_autoptr(SysprofSymbol) symbol = NULL;

          if (process_info != NULL &&
              sysprof_symbol_cache_lookup (process_info->symbol_cache, address) != NULL)
            continue;

          if ((symbol = _sysprof_symbolizer_symbolize (symbolizer, strings, process_info, last_context, address)))
            sysprof_symbol_cache_take (process_info->symbol_cache, g_steal_pointer (&symbol));
        }
    }
}

static DexFuture *
symbolize_fiber (gpointer user_data)
{
  Symbolize *state = user_data;
  g_autoptr(GListModel) model = NULL;
  guint partition;
  guint count;
  guint n_items;
  guint max;

  g_assert (state != NULL);
  g_assert (SYSPROF_IS_DOCUMENT (state->document));
  g_assert (SYSPROF_IS_SYMBOLIZER (state->symbolizer));
  g_assert (SYSPROF_IS_DOCUMENT_SYMBOLS (state->symbols));
  g_assert (state->n_partitions > 1);

  if (SYSPROF_IS_NO_SYMBOLIZER (state->symbolizer))
    return dex_future_new_for_boolean (TRUE);

  model = sysprof_document_list_traceables (state->document);
  n_items = g_list_model_get_n_items (model);
  partition = g_atomic_int_add (&state->partition_seq, 1);
  count = n_items / (state->n_partitions - 1);
  max = MIN (n_items, (partition + 1) * count);

  for (guint i = count * partition; i < max; i++)
    {
      g_autoptr(SysprofDocumentTraceable) traceable = g_list_model_get_item (model, i);
      int pid = sysprof_document_frame_get_pid (SYSPROF_DOCUMENT_FRAME (traceable));
      SysprofProcessInfo *process_info = g_hash_table_lookup (state->pid_to_process_info, GINT_TO_POINTER (pid));

      add_traceable (state->symbols,
                     state->strings,
                     process_info,
                     traceable,
                     state->symbolizer);

      if (g_atomic_int_add (&state->progress, 1) % 100 == 0)
        {
          if (state->progress_func)
            state->progress_func (state->progress / (double)n_items, _("Symbolizing stack traces"), state->progress_data);
        }
    }

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
handle_symbolize_result (DexFuture *completed,
                         gpointer   user_data)
{
  Symbolize *state = user_data;

  if (dex_future_get_status (completed) == DEX_FUTURE_STATUS_REJECTED)
    return NULL;

  return dex_future_new_take_object (g_object_ref (state->symbols));
}

static const struct {
  const char *name;
  guint value;
} context_switches[] = {
  { "- - Hypervisor - -", SYSPROF_ADDRESS_CONTEXT_HYPERVISOR },
  { "- - Kernel - -", SYSPROF_ADDRESS_CONTEXT_KERNEL },
  { "- - User - -", SYSPROF_ADDRESS_CONTEXT_USER },
  { "- - Guest - -", SYSPROF_ADDRESS_CONTEXT_GUEST },
  { "- - Guest Kernel - -", SYSPROF_ADDRESS_CONTEXT_GUEST_KERNEL },
  { "- - Guest User - -", SYSPROF_ADDRESS_CONTEXT_GUEST_USER },
};

void
_sysprof_document_symbols_new (SysprofDocument     *document,
                               SysprofStrings      *strings,
                               SysprofSymbolizer   *symbolizer,
                               GHashTable          *pid_to_process_info,
                               ProgressFunc         progress_func,
                               gpointer             progress_data,
                               GDestroyNotify       progress_data_destroy,
                               GCancellable        *cancellable,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
  g_autoptr(GRefString) context_switch = g_ref_string_new_intern ("Context Switch");
  g_autoptr(DexAsyncResult) result = NULL;
  g_autoptr(GPtrArray) futures = NULL;
  DexFuture *future;
  Symbolize *state;

  g_return_if_fail (SYSPROF_IS_DOCUMENT (document));
  g_return_if_fail (SYSPROF_IS_SYMBOLIZER (symbolizer));

  state = g_new0 (Symbolize, 1);
  state->document = g_object_ref (document);
  state->symbolizer = g_object_ref (symbolizer);
  state->symbols = g_object_new (SYSPROF_TYPE_DOCUMENT_SYMBOLS, NULL);
  state->strings = sysprof_strings_ref (strings);
  state->pid_to_process_info = g_hash_table_ref (pid_to_process_info);
  state->progress_func = progress_func;
  state->progress_data = progress_data;
  state->progress_data_destroy = progress_data_destroy;
  state->n_partitions = MAX (1, g_get_num_processors () / 2) + 1;

  /* Create static symbols for context switch use */
  for (guint cs = 0; cs < G_N_ELEMENTS (context_switches); cs++)
    {
      g_autoptr(SysprofSymbol) symbol = _sysprof_symbol_new (g_ref_string_new_intern (context_switches[cs].name),
                                                             NULL,
                                                             g_ref_string_acquire (context_switch),
                                                             0, 0,
                                                             SYSPROF_SYMBOL_KIND_CONTEXT_SWITCH);

      /* TODO: It would be nice if we had enough insight from the capture header
       * as to the host system, so we can show "vmlinuz" and "Linux" respectively
       * for binary-path and binary-nick when the capture came from Linux.
       */

      state->symbols->context_switches[context_switches[cs].value] = g_steal_pointer (&symbol);
    }

  futures = g_ptr_array_new_with_free_func (dex_unref);

  for (guint i = 0; i < state->n_partitions; i++)
    {
      DexScheduler *scheduler = dex_thread_pool_scheduler_get_default ();

      g_ptr_array_add (futures,
                       dex_scheduler_spawn (scheduler, 0,
                                            symbolize_fiber,
                                            state,
                                            NULL));
    }

  future = dex_future_allv ((DexFuture **)futures->pdata, futures->len);
  future = dex_future_finally (future,
                               handle_symbolize_result,
                               state,
                               (GDestroyNotify)symbolize_free);

  result = dex_async_result_new (NULL, cancellable, callback, user_data);
  dex_async_result_await (result, future);
}

SysprofDocumentSymbols *
_sysprof_document_symbols_new_finish (GAsyncResult  *result,
                                      GError       **error)
{
  g_return_val_if_fail (DEX_IS_ASYNC_RESULT (result), NULL);

  return dex_async_result_propagate_pointer (DEX_ASYNC_RESULT (result), error);
}

/**
 * _sysprof_document_symbols_lookup:
 * @self: a #SysprofDocumentSymbols
 * @process_info: (nullable): the process info if necessary
 * @context: the #SysprofAddressContext for the address
 * @address: a #SysprofAddress to lookup the symbol for
 *
 * Locates the symbol that is found at @address within @context of @pid.
 *
 * Returns: (transfer none) (nullable): a #SysprofSymbol or %NULL
 */
SysprofSymbol *
_sysprof_document_symbols_lookup (SysprofDocumentSymbols   *self,
                                  const SysprofProcessInfo *process_info,
                                  SysprofAddressContext     context,
                                  SysprofAddress            address)
{
  SysprofAddressContext new_context;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_SYMBOLS (self), NULL);
  g_return_val_if_fail (context <= SYSPROF_ADDRESS_CONTEXT_GUEST_USER, NULL);

  if (context == SYSPROF_ADDRESS_CONTEXT_NONE)
    context = SYSPROF_ADDRESS_CONTEXT_USER;

  if (sysprof_address_is_context_switch (address, &new_context))
    return self->context_switches[context];

  if (context == SYSPROF_ADDRESS_CONTEXT_KERNEL)
    return sysprof_symbol_cache_lookup (self->kernel_symbols, address);

  if (process_info != NULL)
    return sysprof_symbol_cache_lookup (process_info->symbol_cache, address);

  return NULL;
}
