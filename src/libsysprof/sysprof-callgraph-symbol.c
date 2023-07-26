/* sysprof-callgraph-symbol.c
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

#include "sysprof-callgraph-private.h"
#include "sysprof-callgraph-symbol-private.h"

struct _SysprofCallgraphSymbol
{
  GObject           parent_instance;
  SysprofCallgraph *callgraph;
  SysprofSymbol    *symbol;
};

enum {
  PROP_0,
  PROP_CALLGRAPH,
  PROP_SYMBOL,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofCallgraphSymbol, sysprof_callgraph_symbol, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static void
sysprof_callgraph_symbol_finalize (GObject *object)
{
  SysprofCallgraphSymbol *self = (SysprofCallgraphSymbol *)object;

  g_clear_object (&self->callgraph);
  g_clear_object (&self->symbol);

  G_OBJECT_CLASS (sysprof_callgraph_symbol_parent_class)->finalize (object);
}

static void
sysprof_callgraph_symbol_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  SysprofCallgraphSymbol *self = SYSPROF_CALLGRAPH_SYMBOL (object);

  switch (prop_id)
    {
    case PROP_CALLGRAPH:
      g_value_set_object (value, self->callgraph);
      break;

    case PROP_SYMBOL:
      g_value_set_object (value, sysprof_callgraph_symbol_get_symbol (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_callgraph_symbol_class_init (SysprofCallgraphSymbolClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_callgraph_symbol_finalize;
  object_class->get_property = sysprof_callgraph_symbol_get_property;

  properties [PROP_CALLGRAPH] =
    g_param_spec_object ("callgraph", NULL, NULL,
                         SYSPROF_TYPE_CALLGRAPH,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SYMBOL] =
    g_param_spec_object ("symbol", NULL, NULL,
                         SYSPROF_TYPE_SYMBOL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_callgraph_symbol_init (SysprofCallgraphSymbol *self)
{
}

SysprofCallgraphSymbol *
_sysprof_callgraph_symbol_new (SysprofCallgraph *callgraph,
                               SysprofSymbol    *symbol)
{
  SysprofCallgraphSymbol *self;

  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH (callgraph), NULL);
  g_return_val_if_fail (SYSPROF_IS_SYMBOL (symbol), NULL);

  self = g_object_new (SYSPROF_TYPE_CALLGRAPH_SYMBOL, NULL);
  g_set_object (&self->callgraph, callgraph);
  g_set_object (&self->symbol, symbol);

  return self;
}

/**
 * sysprof_callgraph_symbol_get_symbol:
 * @self: a #SysprofCallgraphSymbol
 *
 * Gets the symbol for the symbol.
 *
 * Returns: (nullable) (transfer none): a #SysprofSymbol
 */
SysprofSymbol *
sysprof_callgraph_symbol_get_symbol (SysprofCallgraphSymbol *self)
{
  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH_SYMBOL (self), NULL);

  return self->symbol;
}

/**
 * sysprof_callgraph_symbol_get_summary_augment: (skip)
 * @self: a #SysprofCallgraphSymbol
 *
 * Gets the augmentation that was attached to the summary for
 * the callgraph node's symbol.
 *
 * Returns: (nullable) (transfer none): the augmentation data
 */
gpointer
sysprof_callgraph_symbol_get_summary_augment (SysprofCallgraphSymbol *self)
{
  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH_SYMBOL (self), NULL);

  if (self->callgraph == NULL)
    return NULL;

  return _sysprof_callgraph_get_symbol_augment (self->callgraph, self->symbol);
}

/**
 * sysprof_callgraph_symbol_get_callgraph:
 * @self: a #SysprofCallgraphSymbol
 *
 * Gets the callgraph the symbol belongs to.
 *
 * Returns: (transfer none) (nullable): a #SysprofCallgraph, or %NULL
 */
SysprofCallgraph *
sysprof_callgraph_symbol_get_callgraph (SysprofCallgraphSymbol *self)
{
  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH_SYMBOL (self), NULL);

  return self->callgraph;
}

typedef struct _SysprofCallgraphSymbolListModel
{
  GObject parent_instance;
  SysprofCallgraph *callgraph;
  GPtrArray *symbols;
} SysprofCallgraphSymbolListModel;

static guint
sysprof_callgraph_symbol_list_model_get_n_items (GListModel *model)
{
  SysprofCallgraphSymbolListModel *self = (SysprofCallgraphSymbolListModel *)model;

  if (self->symbols != NULL)
    return self->symbols->len;

  return 0;
}

static GType
sysprof_callgraph_symbol_list_model_get_item_type (GListModel *model)
{
  return SYSPROF_TYPE_CALLGRAPH_SYMBOL;
}

static gpointer
sysprof_callgraph_symbol_list_model_get_item (GListModel *model,
                                              guint       position)
{
  SysprofCallgraphSymbolListModel *self = (SysprofCallgraphSymbolListModel *)model;

  if (self->symbols == NULL || position >= self->symbols->len || self->callgraph == NULL)
    return NULL;

  return _sysprof_callgraph_symbol_new (self->callgraph,
                                        g_ptr_array_index (self->symbols, position));
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_n_items = sysprof_callgraph_symbol_list_model_get_n_items;
  iface->get_item_type = sysprof_callgraph_symbol_list_model_get_item_type;
  iface->get_item = sysprof_callgraph_symbol_list_model_get_item;
}

#define SYSPROF_TYPE_CALLGRAPH_SYMBOL_LIST_MODEL (sysprof_callgraph_symbol_list_model_get_type())
G_DECLARE_FINAL_TYPE (SysprofCallgraphSymbolListModel, sysprof_callgraph_symbol_list_model, SYSPROF, CALLGRAPH_SYMBOL_LIST_MODEL, GObject)
G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofCallgraphSymbolListModel, sysprof_callgraph_symbol_list_model, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

static void
sysprof_callgraph_symbol_list_model_dispose (GObject *object)
{
  SysprofCallgraphSymbolListModel *self = (SysprofCallgraphSymbolListModel *)object;

  g_clear_pointer (&self->symbols, g_ptr_array_unref);
  g_clear_object (&self->callgraph);

  G_OBJECT_CLASS (sysprof_callgraph_symbol_parent_class)->dispose (object);
}

static void
sysprof_callgraph_symbol_list_model_class_init (SysprofCallgraphSymbolListModelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = sysprof_callgraph_symbol_list_model_dispose;
}

static void
sysprof_callgraph_symbol_list_model_init (SysprofCallgraphSymbolListModel *self)
{
}

GListModel *
_sysprof_callgraph_symbol_list_model_new (SysprofCallgraph *callgraph,
                                          GPtrArray        *symbols)
{
  SysprofCallgraphSymbolListModel *self;

  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH (callgraph), NULL);

  self = g_object_new (SYSPROF_TYPE_CALLGRAPH_SYMBOL_LIST_MODEL, NULL);
  g_set_object (&self->callgraph, callgraph);

  if (symbols != NULL)
    self->symbols = g_ptr_array_ref (symbols);

  return G_LIST_MODEL (self);
}

G_END_DECLS
