/* sysprof-callgraph.c
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

#include "timsort/gtktimsortprivate.h"

#include "sysprof-callgraph-private.h"
#include "sysprof-callgraph-frame-private.h"
#include "sysprof-callgraph-symbol-private.h"
#include "sysprof-descendants-model-private.h"
#include "sysprof-document-bitset-index-private.h"
#include "sysprof-document-private.h"
#include "sysprof-document-traceable.h"
#include "sysprof-symbol-private.h"

#include "eggbitset.h"

#define MAX_STACK_DEPTH     1024
#define INLINE_AUGMENT_SIZE (GLIB_SIZEOF_VOID_P*2)

static GType
sysprof_callgraph_get_item_type (GListModel *model)
{
  return SYSPROF_TYPE_CALLGRAPH_FRAME;
}

static guint
sysprof_callgraph_get_n_items (GListModel *model)
{
  return 1;
}

static gpointer
sysprof_callgraph_get_item (GListModel *model,
                            guint       position)
{
  SysprofCallgraph *self = SYSPROF_CALLGRAPH (model);

  if (position > 0)
    return NULL;

  return _sysprof_callgraph_frame_new_for_node (self, NULL, &self->root);
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = sysprof_callgraph_get_item_type;
  iface->get_n_items = sysprof_callgraph_get_n_items;
  iface->get_item = sysprof_callgraph_get_item;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofCallgraph, sysprof_callgraph, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

static SysprofSymbol *everything;
static SysprofSymbol *untraceable;

static void
sysprof_callgraph_summary_free_all (SysprofCallgraphSummary *summary)
{
  g_clear_pointer (&summary->augment[0], g_free);
  summary->augment[1] = NULL;
  g_clear_pointer (&summary->callers, g_ptr_array_unref);
  g_clear_pointer (&summary->traceables, egg_bitset_unref);
  g_free (summary);
}

static void
sysprof_callgraph_summary_free_self (SysprofCallgraphSummary *summary)
{
  summary->augment[0] = NULL;
  summary->augment[1] = NULL;
  g_clear_pointer (&summary->callers, g_ptr_array_unref);
  g_clear_pointer (&summary->traceables, egg_bitset_unref);
  g_free (summary);
}

static inline SysprofCallgraphSummary *
sysprof_callgraph_get_summary (SysprofCallgraph *self,
                               SysprofSymbol    *symbol)
{
  SysprofCallgraphSummary *summary;

  if G_UNLIKELY (!(summary = g_hash_table_lookup (self->symbol_to_summary, symbol)))
    {
      summary = g_new0 (SysprofCallgraphSummary, 1);
      summary->traceables = egg_bitset_new_empty ();
      summary->callers = g_ptr_array_new ();
      summary->symbol = symbol;

      g_hash_table_insert (self->symbol_to_summary, symbol, summary);
      g_ptr_array_add (self->symbols, symbol);
    }

  return summary;
}

static void
sysprof_callgraph_dispose (GObject *object)
{
  SysprofCallgraph *self = (SysprofCallgraph *)object;
  GDestroyNotify notify = self->augment_func_data_destroy;
  gpointer notify_data = self->augment_func_data;

  self->augment_size = 0;
  self->augment_func = NULL;
  self->augment_func_data = NULL;
  self->augment_func_data_destroy = NULL;

  if (notify != NULL)
    notify (notify_data);

  G_OBJECT_CLASS (sysprof_callgraph_parent_class)->dispose (object);
}

static void
sysprof_callgraph_finalize (GObject *object)
{
  SysprofCallgraph *self = (SysprofCallgraph *)object;

  g_clear_pointer (&self->symbol_to_summary, g_hash_table_unref);
  g_clear_pointer (&self->symbols, g_ptr_array_unref);

  g_clear_object (&self->document);
  g_clear_object (&self->traceables);

  g_clear_pointer (&self->allocator, sysprof_allocator_unref);

  G_OBJECT_CLASS (sysprof_callgraph_parent_class)->finalize (object);
}

static void
sysprof_callgraph_class_init (SysprofCallgraphClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = sysprof_callgraph_dispose;
  object_class->finalize = sysprof_callgraph_finalize;

  everything = _sysprof_symbol_new (g_ref_string_new_intern ("All Processes"),
                                    NULL, NULL, 0, 0,
                                    SYSPROF_SYMBOL_KIND_ROOT);
  untraceable = _sysprof_symbol_new (g_ref_string_new_intern ("Unwindable"),
                                     NULL, NULL, 0, 0,
                                     SYSPROF_SYMBOL_KIND_UNWINDABLE);
}

static void
sysprof_callgraph_init (SysprofCallgraph *self)
{
  self->allocator = sysprof_allocator_new ();
}

static void
sysprof_callgraph_populate_callers (SysprofCallgraph     *self,
                                    SysprofCallgraphNode *node,
                                    guint                 list_model_index)
{
  g_assert (SYSPROF_IS_CALLGRAPH (self));
  g_assert (node != NULL);

  for (const SysprofCallgraphNode *iter = node;
       iter != NULL;
       iter = iter->parent)
    {
      egg_bitset_add (iter->summary->traceables, list_model_index);

      if (iter->parent != NULL)
        {
          SysprofSymbol *parent_symbol = iter->parent->summary->symbol;
          SysprofSymbolKind parent_kind = sysprof_symbol_get_kind (parent_symbol);
          guint pos;

          if (parent_kind != SYSPROF_SYMBOL_KIND_PROCESS &&
              parent_kind != SYSPROF_SYMBOL_KIND_ROOT &&
              !g_ptr_array_find (iter->summary->callers, parent_symbol, &pos))
            g_ptr_array_add (iter->summary->callers, parent_symbol);
        }
    }
}

static SysprofCallgraphNode *
sysprof_callgraph_add_trace (SysprofCallgraph  *self,
                             SysprofSymbol    **symbols,
                             guint              n_symbols,
                             guint              list_model_index,
                             gboolean           hide_system_libraries)
{
  SysprofCallgraphNode *parent = NULL;

  g_assert (SYSPROF_IS_CALLGRAPH (self));
  g_assert (n_symbols >= 2);
  g_assert (symbols[n_symbols-1] == everything);

  parent = &self->root;
  parent->count++;

  for (guint i = n_symbols - 1; i > 0; i--)
    {
      SysprofSymbol *symbol = symbols[i-1];
      SysprofCallgraphNode *node = NULL;

      if (hide_system_libraries && _sysprof_symbol_is_system_library (symbol))
        continue;

      /* Try to find @symbol within the children of @parent */
      for (SysprofCallgraphNode *iter = parent->children;
           iter != NULL;
           iter = iter->next)
        {
          g_assert (iter != NULL);
          g_assert (iter->summary != NULL);
          g_assert (iter->summary->symbol != NULL);
          g_assert (symbol != NULL);

          if (_sysprof_symbol_equal (iter->summary->symbol, symbol))
            {
              node = iter;
              node->count++;
              goto next_symbol;
            }
        }

      /* Otherwise create a new node */
      node = sysprof_allocator_new0 (self->allocator, SysprofCallgraphNode);
      node->summary = sysprof_callgraph_get_summary (self, symbol);
      node->parent = parent;
      node->next = parent->children;
      node->count = 1;
      if (parent->children)
        parent->children->prev = node;
      parent->children = node;

    next_symbol:
      parent = node;
    }

  sysprof_callgraph_populate_callers (self, parent, list_model_index);

  return parent;
}

static void
reverse_symbols (SysprofSymbol **symbols,
                 guint           n_symbols)
{
  guint half = n_symbols / 2;

  for (guint i = 0; i < half; i++)
    {
      SysprofSymbol *tmp = symbols[i];
      symbols[i] = symbols[n_symbols-1-i];
      symbols[n_symbols-1-i] = tmp;
    }
}

void
_sysprof_callgraph_categorize (SysprofCallgraph     *self,
                               SysprofCallgraphNode *node)
{
  if (node->category)
    return;

  if (node->parent && node->parent->category == 0)
    _sysprof_callgraph_categorize (self, node->parent);

  switch (node->summary->symbol->kind)
    {
    case SYSPROF_SYMBOL_KIND_ROOT:
    case SYSPROF_SYMBOL_KIND_THREAD:
    case SYSPROF_SYMBOL_KIND_PROCESS:
      node->category = SYSPROF_CALLGRAPH_CATEGORY_PRESENTATION;
      break;

    case SYSPROF_SYMBOL_KIND_CONTEXT_SWITCH:
      node->category = SYSPROF_CALLGRAPH_CATEGORY_CONTEXT_SWITCH;
      break;

    case SYSPROF_SYMBOL_KIND_UNWINDABLE:
      node->category = SYSPROF_CALLGRAPH_CATEGORY_UNWINDABLE;
      break;

    case SYSPROF_SYMBOL_KIND_KERNEL:
    case SYSPROF_SYMBOL_KIND_USER:
      node->category = _sysprof_callgraph_node_categorize (node);

      if (node->category > SYSPROF_CALLGRAPH_CATEGORY_UNCATEGORIZED)
        break;

      G_GNUC_FALLTHROUGH;

    default:
      {
        SysprofCallgraphNode *parent = node->parent;

        while (parent != NULL)
          {
            if (parent->category & SYSPROF_CALLGRAPH_CATEGORY_INHERIT)
              {
                node->category = parent->category;
                return;
              }

            parent = parent->parent;
          }

        node->category = SYSPROF_CALLGRAPH_CATEGORY_UNCATEGORIZED;
        break;
      }
    }
}

static void
sysprof_callgraph_add_traceable (SysprofCallgraph         *self,
                                 SysprofDocumentTraceable *traceable,
                                 guint                     list_model_index)
{
  SysprofAddressContext final_context;
  SysprofCallgraphNode *node;
  SysprofSymbol **symbols;
  SysprofSymbol *process_symbol;
  guint stack_depth;
  guint n_symbols;
  int pid;
  int tid;

  g_assert (SYSPROF_IS_CALLGRAPH (self));
  g_assert (SYSPROF_IS_DOCUMENT_TRACEABLE (traceable));

  pid = sysprof_document_frame_get_pid (SYSPROF_DOCUMENT_FRAME (traceable));

  /* Ignore "Process 0" (the Idle process) if requested */
  if (pid == 0 && (self->flags & SYSPROF_CALLGRAPH_FLAGS_IGNORE_PROCESS_0) != 0)
    return;

  /* Ignore kernel processes if requested */
  process_symbol = _sysprof_document_process_symbol (self->document, pid, !!(self->flags & SYSPROF_CALLGRAPH_FLAGS_MERGE_SIMILAR_PROCESSES));
  if (process_symbol->is_kernel_process && (self->flags & SYSPROF_CALLGRAPH_FLAGS_IGNORE_KERNEL_PROCESSES))
    return;

  tid = sysprof_document_traceable_get_thread_id (traceable);

  /* Early ignore anything with empty or too large a stack */
  stack_depth = sysprof_document_traceable_get_stack_depth (traceable);
  if (stack_depth == 0 || stack_depth > MAX_STACK_DEPTH)
    return;

  symbols = g_newa (SysprofSymbol *, stack_depth + 4);
  n_symbols = sysprof_document_symbolize_traceable (self->document,
                                                    traceable,
                                                    symbols,
                                                    stack_depth,
                                                    &final_context);
  if (n_symbols == 0)
    return;

  g_assert (n_symbols <= stack_depth);

  /* Sometimes we get a very unhelpful unwind from the capture
   * which is basically a single frame of "user space context".
   * That means we got no amount of the stack, but we should
   * really account costs to something in the application other
   * than the [Application] entry itself so that it's more clear
   * that it was a corrupted unwind when recording.
   */
  if (n_symbols == 1 &&
      _sysprof_symbol_is_context_switch (symbols[0]) &&
      final_context == SYSPROF_ADDRESS_CONTEXT_USER)
    symbols[0] = untraceable;

  /* We saved 3 extra spaces for these above so that we can
   * tack on the "Process" symbol and the "All Processes" symbol.
   * If the final address context places us in Kernel, we want
   * to add a "- - Kernel - -" symbol to ensure that we are
   * accounting cost to the kernel for the process.
   */
  if (final_context == SYSPROF_ADDRESS_CONTEXT_KERNEL)
    symbols[n_symbols++] = _sysprof_document_kernel_symbol (self->document);

  /* If the first thing we see is a context switch, then there is
   * nothing after it to account for. Just skip the symbol as it
   * provides nothing to us in the callgraph.
   */
  if (_sysprof_symbol_is_context_switch (symbols[0]))
    {
      symbols++;
      n_symbols--;
    }

  if ((self->flags & SYSPROF_CALLGRAPH_FLAGS_BOTTOM_UP) != 0)
    reverse_symbols (symbols, n_symbols);

  /* If the user requested thread-ids within each process, then
   * insert a symbol for that before the real stacks.
   */
  if ((self->flags & SYSPROF_CALLGRAPH_FLAGS_INCLUDE_THREADS) != 0)
    symbols[n_symbols++] = _sysprof_document_thread_symbol (self->document, pid, tid);

  symbols[n_symbols++] = process_symbol;
  symbols[n_symbols++] = everything;

  if (n_symbols > self->height)
    self->height = n_symbols;

  node = sysprof_callgraph_add_trace (self,
                                      symbols,
                                      n_symbols,
                                      list_model_index,
                                      !!(self->flags & SYSPROF_CALLGRAPH_FLAGS_HIDE_SYSTEM_LIBRARIES));

  if (node != NULL)
    {
      node->is_toplevel = TRUE;

      if (self->augment_func)
        self->augment_func (self,
                            node,
                            SYSPROF_DOCUMENT_FRAME (traceable),
                            TRUE,
                            self->augment_func_data);
    }

  if ((self->flags & SYSPROF_CALLGRAPH_FLAGS_CATEGORIZE_FRAMES) != 0)
    _sysprof_callgraph_categorize (self, node);
}

static int
sort_by_symbol_name (gconstpointer a,
                     gconstpointer b)
{
  const SysprofCallgraphNode *node_a = *(const SysprofCallgraphNode * const *)a;
  const SysprofCallgraphNode *node_b = *(const SysprofCallgraphNode * const *)b;

  return g_utf8_collate (node_a->summary->symbol->name, node_b->summary->symbol->name);
}

static int
sort_by_count (gconstpointer a,
               gconstpointer b)
{
  const SysprofCallgraphNode *node_a = *(const SysprofCallgraphNode * const *)a;
  const SysprofCallgraphNode *node_b = *(const SysprofCallgraphNode * const *)b;

  if (node_a->count > node_b->count)
    return -1;

  if (node_a->count < node_b->count)
    return 1;

  return 0;
}

static void
sort_children (SysprofCallgraphNode  *node,
               SysprofCallgraphFlags  flags)
{
  SysprofCallgraphNode **children;
  guint n_children = 0;
  guint i = 0;

  if (node->children == NULL)
    return;

  for (SysprofCallgraphNode *child = node->children; child; child = child->next)
    {
      sort_children (child, flags);
      n_children++;
    }

  children = g_alloca (sizeof (SysprofCallgraphNode *) * (n_children + 1));
  for (SysprofCallgraphNode *child = node->children; child; child = child->next)
    children[i++] = child;
  children[i] = NULL;

  if (flags & SYSPROF_CALLGRAPH_FLAGS_LEFT_HEAVY)
    qsort (children, n_children, sizeof (SysprofCallgraphNode *), sort_by_count);
  else
    qsort (children, n_children, sizeof (SysprofCallgraphNode *), sort_by_symbol_name);

  node->children = children[0];
  node->children->prev = NULL;
  node->children->next = children[1];

  for (i = 1; i < n_children; i++)
    {
      children[i]->next = children[i+1];
      children[i]->prev = children[i-1];
    }
}

static void
sysprof_callgraph_new_worker (GTask        *task,
                              gpointer      source_object,
                              gpointer      task_data,
                              GCancellable *cancellable)
{
  SysprofCallgraph *self = task_data;
  guint n_items;

  g_assert (G_IS_TASK (task));
  g_assert (source_object == NULL);
  g_assert (SYSPROF_IS_CALLGRAPH (self));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  n_items = g_list_model_get_n_items (self->traceables);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(SysprofDocumentTraceable) traceable = g_list_model_get_item (self->traceables, i);

      /* Currently, we might be racing against changes in the model. And we
       * definitely need to address that. But this at least keeps things
       * recovering in the meantime.
       */
      if (traceable == NULL)
        break;

      sysprof_callgraph_add_traceable (self, traceable, i);
    }

  /* Sort callgraph nodes alphabetically so that we can use them in the
   * flamegraph without any further processing.
   */
  sort_children (&self->root, self->flags);

  g_task_return_pointer (task, g_object_ref (self), g_object_unref);
}

void
_sysprof_callgraph_new_async (SysprofDocument         *document,
                              SysprofCallgraphFlags    flags,
                              GListModel              *traceables,
                              gsize                    augment_size,
                              SysprofAugmentationFunc  augment_func,
                              gpointer                 augment_func_data,
                              GDestroyNotify           augment_func_data_destroy,
                              GCancellable            *cancellable,
                              GAsyncReadyCallback      callback,
                              gpointer                 user_data)
{
  g_autoptr(SysprofCallgraph) self = NULL;
  g_autoptr(GTask) task = NULL;
  GDestroyNotify summary_free;

  g_return_if_fail (SYSPROF_IS_DOCUMENT (document));
  g_return_if_fail (G_IS_LIST_MODEL (traceables));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  if (augment_size > INLINE_AUGMENT_SIZE)
    summary_free = (GDestroyNotify)sysprof_callgraph_summary_free_all;
  else
    summary_free = (GDestroyNotify)sysprof_callgraph_summary_free_self;

  self = g_object_new (SYSPROF_TYPE_CALLGRAPH, NULL);
  self->flags = flags;
  self->document = g_object_ref (document);
  self->traceables = g_object_ref (traceables);
  self->augment_size = augment_size;
  self->augment_func = augment_func;
  self->augment_func_data = augment_func_data;
  self->augment_func_data_destroy = augment_func_data_destroy;
  self->symbol_to_summary = g_hash_table_new_full ((GHashFunc)sysprof_symbol_hash,
                                                   (GEqualFunc)sysprof_symbol_equal,
                                                   NULL,
                                                   summary_free);
  self->symbols = g_ptr_array_new ();
  self->root.summary = sysprof_callgraph_get_summary (self, everything);

  task = g_task_new (NULL, cancellable, callback, user_data);
  g_task_set_source_tag (task, _sysprof_callgraph_new_async);
  g_task_set_task_data (task, g_object_ref (self), g_object_unref);
  g_task_run_in_thread (task, sysprof_callgraph_new_worker);
}

SysprofCallgraph *
_sysprof_callgraph_new_finish (GAsyncResult  *result,
                               GError       **error)
{
  g_return_val_if_fail (G_IS_TASK (result), NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

static inline gpointer
get_augmentation (SysprofCallgraph *self,
                  gpointer         *augment_location)
{
  if (self->augment_size == 0)
    return NULL;

  if (self->augment_size <= INLINE_AUGMENT_SIZE)
    return augment_location;

  if (*augment_location == NULL)
    *augment_location = g_malloc0 (self->augment_size);

  return *augment_location;
}

gpointer
sysprof_callgraph_get_augment (SysprofCallgraph     *self,
                               SysprofCallgraphNode *node)
{
  if (node == NULL)
    node = &self->root;

  return get_augmentation (self, &node->augment[0]);
}

gpointer
sysprof_callgraph_get_summary_augment (SysprofCallgraph     *self,
                                       SysprofCallgraphNode *node)
{
  if (node == NULL)
    node = &self->root;

  return get_augmentation (self, &node->summary->augment[0]);
}

gpointer
_sysprof_callgraph_get_symbol_augment (SysprofCallgraph *self,
                                       SysprofSymbol    *symbol)
{
  SysprofCallgraphSummary *summary;

  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH (self), NULL);
  g_return_val_if_fail (SYSPROF_IS_SYMBOL (symbol), NULL);

  if ((summary = g_hash_table_lookup (self->symbol_to_summary, symbol)))
    return get_augmentation (self, &summary->augment[0]);

  return NULL;
}

/**
 * sysprof_callgraph_node_parent: (skip)
 */
SysprofCallgraphNode *
sysprof_callgraph_node_parent (SysprofCallgraphNode *node)
{
  return node->parent;
}

/**
 * sysprof_callgraph_list_callers:
 * @self: a #SysprofCallgraph
 * @symbol: a #SysprofSymbol
 *
 * Gets a list of #SysprofSymbol that call @symbol.
 *
 * Returns: (transfer full): a #GListModel of #SysprofCallgraphSymbol
 */
GListModel *
sysprof_callgraph_list_callers (SysprofCallgraph *self,
                                SysprofSymbol    *symbol)
{
  SysprofCallgraphSummary *summary;

  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH (self), NULL);
  g_return_val_if_fail (SYSPROF_IS_SYMBOL (symbol), NULL);

  if ((summary = g_hash_table_lookup (self->symbol_to_summary, symbol)))
    return _sysprof_callgraph_symbol_list_model_new (self, summary->callers);

  return G_LIST_MODEL (g_list_store_new (SYSPROF_TYPE_CALLGRAPH_SYMBOL));
}

/**
 * sysprof_callgraph_list_traceables_for_symbol:
 * @self: a #SysprofCallgraph
 * @symbol: a #SysprofSymbol
 *
 * Gets a list of all the #SysprofTraceable within the callgraph
 * that contain @symbol.
 *
 * Returns: (transfer full): a #GListModel of #SysprofDocumentTraceable
 */
GListModel *
sysprof_callgraph_list_traceables_for_symbol (SysprofCallgraph *self,
                                              SysprofSymbol    *symbol)
{
  SysprofCallgraphSummary *summary;

  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH (self), NULL);
  g_return_val_if_fail (SYSPROF_IS_SYMBOL (symbol), NULL);

  if ((summary = g_hash_table_lookup (self->symbol_to_summary, symbol)))
    return _sysprof_document_bitset_index_new (self->traceables, summary->traceables);

  return G_LIST_MODEL (g_list_store_new (SYSPROF_TYPE_DOCUMENT_TRACEABLE));
}

/**
 * sysprof_callgraph_list_traceables_for_symbols_matching:
 *
 * Returns: (transfer full): a [iface@Gio.ListModel] of [iface@Sysprof.DocumentTraceable]
 */
GListModel *
sysprof_callgraph_list_traceables_for_symbols_matching (SysprofCallgraph *self,
                                                        const char       *pattern)
{
  g_autoptr(GPatternSpec) pspec = NULL;
  g_autoptr(EggBitset) bitset = NULL;

  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH (self), NULL);

  if (pattern == NULL || pattern[0] == 0)
    return g_object_ref (self->traceables);

  pspec = g_pattern_spec_new (pattern);
  bitset = egg_bitset_new_empty ();

  for (guint i = 0; i < self->symbols->len; i++)
    {
      SysprofSymbol *symbol = g_ptr_array_index (self->symbols, i);
      const char *name = sysprof_symbol_get_name (symbol);

      if (g_pattern_spec_match (pspec, strlen (name), name, NULL))
        {
          SysprofCallgraphSummary *summary = g_hash_table_lookup (self->symbol_to_summary, symbol);

          if (summary != NULL)
            egg_bitset_union (bitset, summary->traceables);
        }
    }

  return _sysprof_document_bitset_index_new (self->traceables, bitset);
}

/**
 * sysprof_callgraph_list_symbols:
 * @self: a #SysprofCallgraph
 *
 * Gets a #GListModel of #SysprofCallgraphSymbol that an be used to
 * display a function list and associated augmentation data.
 *
 * Returns: (transfer full): a #GListModel of #SysprofCallgraphSymbol
 */
GListModel *
sysprof_callgraph_list_symbols (SysprofCallgraph *self)
{
  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH (self), NULL);

  return _sysprof_callgraph_symbol_list_model_new (self, self->symbols);
}

static void
sysprof_callgraph_descendants_worker (GTask        *task,
                                      gpointer      source_object,
                                      gpointer      task_data,
                                      GCancellable *cancellable)
{
  SysprofCallgraph *self = source_object;
  SysprofSymbol *symbol = task_data;

  g_assert (G_IS_TASK (task));
  g_assert (SYSPROF_IS_CALLGRAPH (self));
  g_assert (SYSPROF_IS_SYMBOL (symbol));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  g_task_return_pointer (task,
                         _sysprof_descendants_model_new (self, symbol),
                         g_object_unref);
}

void
sysprof_callgraph_descendants_async (SysprofCallgraph    *self,
                                     SysprofSymbol       *symbol,
                                     GCancellable        *cancellable,
                                     GAsyncReadyCallback  callback,
                                     gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (SYSPROF_IS_CALLGRAPH (self));
  g_return_if_fail (SYSPROF_IS_SYMBOL (symbol));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_callgraph_descendants_async);
  g_task_set_task_data (task, g_object_ref (symbol), g_object_unref);
  g_task_run_in_thread (task, sysprof_callgraph_descendants_worker);
}

/**
 * sysprof_callgraph_descendants_finish:
 *
 * Returns: (transfer full):
 */
GListModel *
sysprof_callgraph_descendants_finish (SysprofCallgraph  *self,
                                      GAsyncResult      *result,
                                      GError           **error)
{
  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH (self), NULL);
  g_return_val_if_fail (G_IS_TASK (result), NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

typedef struct _FilterByPrefix
{
  SysprofDocument *document;
  GListModel *traceables;
  GPtrArray *prefix;
  EggBitset *bitset;
  guint max_results;
} FilterByPrefix;

static void
filter_by_prefix_free (FilterByPrefix *state)
{
  g_clear_object (&state->document);
  g_clear_object (&state->traceables);
  g_clear_pointer (&state->prefix, g_ptr_array_unref);
  g_clear_pointer (&state->bitset, egg_bitset_unref);
  g_free (state);
}

static gboolean
traceable_has_prefix (SysprofDocument          *document,
                      SysprofDocumentTraceable *traceable,
                      GPtrArray                *prefix)
{
  SysprofAddressContext final_context;
  SysprofSymbol **symbols;
  SysprofAddress *addresses;
  guint s = 0;
  guint stack_depth;
  guint n_symbols;

  stack_depth = sysprof_document_traceable_get_stack_depth (traceable);
  if (stack_depth > MAX_STACK_DEPTH)
    return FALSE;

  addresses = g_alloca (sizeof (SysprofAddress) * stack_depth);
  sysprof_document_traceable_get_stack_addresses (traceable, addresses, stack_depth);

  symbols = g_alloca (sizeof (SysprofSymbol *) * stack_depth);
  n_symbols = sysprof_document_symbolize_traceable (document, traceable, symbols, stack_depth, &final_context);

  if (n_symbols < prefix->len)
    return FALSE;

  for (guint p = 0; p < prefix->len; p++)
    {
      SysprofSymbol *prefix_symbol = g_ptr_array_index (prefix, p);
      gboolean found = FALSE;

      for (; !found && s < n_symbols; s++)
        found = _sysprof_symbol_equal (prefix_symbol, symbols[s]);

      if (!found)
        return FALSE;
    }

  return TRUE;
}

static void
filter_by_prefix_worker (GTask        *task,
                         gpointer      source_object,
                         gpointer      task_data,
                         GCancellable *cancellable)
{
  FilterByPrefix *state = task_data;
  g_autoptr(EggBitset) bitset = NULL;
  SysprofDocument *document;
  GListModel *model;
  GPtrArray *prefix;
  EggBitsetIter iter;
  guint n_results = 0;
  guint i;

  g_assert (G_IS_TASK (task));
  g_assert (SYSPROF_IS_CALLGRAPH (source_object));
  g_assert (state != NULL);
  g_assert (G_IS_LIST_MODEL (state->traceables));
  g_assert (state->prefix != NULL);
  g_assert (state->prefix->len > 0);
  g_assert (state->bitset != NULL);
  g_assert (!egg_bitset_is_empty (state->bitset));

  bitset = egg_bitset_new_empty ();

  model = state->traceables;
  document = state->document;
  prefix = state->prefix;

  if (egg_bitset_iter_init_first (&iter, state->bitset, &i))
    {
      do
        {
          g_autoptr(SysprofDocumentTraceable) traceable = g_list_model_get_item (model, i);

          if (traceable_has_prefix (document, traceable, prefix))
            {
              egg_bitset_add (bitset, i);
              n_results++;
            }
        }
      while (n_results < state->max_results &&
             egg_bitset_iter_next (&iter, &i));
    }

  g_task_return_pointer (task,
                         _sysprof_document_bitset_index_new (model, bitset),
                         g_object_unref);
}

static int
sort_by_size_asc (gconstpointer a,
                  gconstpointer b)
{
  const EggBitset *bitset_a = *(const EggBitset * const *)a;
  const EggBitset *bitset_b = *(const EggBitset * const *)a;
  gsize size_a = egg_bitset_get_size (bitset_a);
  gsize size_b = egg_bitset_get_size (bitset_b);

  if (size_a < size_b)
    return -1;

  if (size_a > size_b)
    return 1;

  return 0;
}

void
sysprof_callgraph_list_traceables_for_node_async (SysprofCallgraph     *self,
                                                  SysprofCallgraphNode *node,
                                                  GCancellable         *cancellable,
                                                  GAsyncReadyCallback   callback,
                                                  gpointer              user_data)
{
  g_autoptr(GTask) task = NULL;
  g_autoptr(GPtrArray) prefix = NULL;
  g_autoptr(GPtrArray) bitsets = NULL;
  g_autoptr(EggBitset) bitset = NULL;
  FilterByPrefix *state;

  g_return_if_fail (SYSPROF_IS_CALLGRAPH (self));
  g_return_if_fail (node != NULL);
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_callgraph_list_traceables_for_node_async);

  prefix = g_ptr_array_new ();
  bitsets = g_ptr_array_new ();

  for (; node; node = node->parent)
      {
        SysprofCallgraphSummary *summary = node->summary;
        SysprofSymbol *symbol = summary->symbol;

        if (symbol->kind != SYSPROF_SYMBOL_KIND_USER &&
            symbol->kind != SYSPROF_SYMBOL_KIND_KERNEL)
          continue;

        g_ptr_array_add (bitsets, summary->traceables);
        g_ptr_array_add (prefix, symbol);
      }

  if (prefix->len == 0)
    {
      g_task_return_pointer (task,
                             g_list_store_new (SYSPROF_TYPE_DOCUMENT_TRACEABLE),
                             g_object_unref);
      return;
    }

  /* Sort the bitsets by size to shrink potential interscetions */
  gtk_tim_sort (bitsets->pdata,
                bitsets->len,
                sizeof (gpointer),
                (GCompareDataFunc)sort_by_size_asc,
                NULL);
  bitset = egg_bitset_copy (g_ptr_array_index (bitsets, 0));
  for (guint i = 1; i < bitsets->len; i++)
    {
      const EggBitset *other = g_ptr_array_index (bitsets, i);
      egg_bitset_intersect (bitset, other);
    }

  if (egg_bitset_is_empty (bitset))
    {
      g_task_return_pointer (task,
                             g_list_store_new (SYSPROF_TYPE_DOCUMENT_TRACEABLE),
                             g_object_unref);
      return;
    }

  state = g_new0 (FilterByPrefix, 1);
  state->document = g_object_ref (self->document);
  state->traceables = g_object_ref (self->traceables);
  state->prefix = g_steal_pointer (&prefix);
  state->bitset = g_steal_pointer (&bitset);
  state->max_results = 1000;

  g_task_set_task_data (task, state, (GDestroyNotify)filter_by_prefix_free);
  g_task_run_in_thread (task, filter_by_prefix_worker);
}

/**
 * sysprof_callgraph_list_traceables_for_node_finish:
 * @self: a [class@Sysprof.Callgraph]
 *
 * Returns: (transfer full):
 */
GListModel *
sysprof_callgraph_list_traceables_for_node_finish (SysprofCallgraph  *self,
                                                   GAsyncResult      *result,
                                                   GError           **error)
{
  GListModel *ret;

  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH (self), NULL);
  g_return_val_if_fail (G_IS_TASK (result), NULL);

  ret = g_task_propagate_pointer (G_TASK (result), error);

  g_return_val_if_fail (!ret || G_IS_LIST_MODEL (ret), NULL);

  return ret;
}
