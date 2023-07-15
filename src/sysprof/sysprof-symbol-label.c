/* sysprof-symbol-label.c
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

#include "sysprof-symbol-label-private.h"

#include "sysprof-symbol-private.h"

#define CSS_CLASS_PROCESS        "process"
#define CSS_CLASS_ROOT           "root"
#define CSS_CLASS_CONTEXT_SWITCH "context-switch"

struct _SysprofSymbolLabel
{
  GtkWidget parent_instance;
  GtkWidget *text;
  SysprofSymbol *symbol;
};

enum {
  PROP_0,
  PROP_SYMBOL,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofSymbolLabel, sysprof_symbol_label, GTK_TYPE_WIDGET)

static GParamSpec *properties [N_PROPS];
static const char *kind_classes[] = {
  NULL,
  "root",
  "process",
  "thread",
  "context-switch",
  "user",
  "kernel",
  "unwindable",
};

static void
sysprof_symbol_label_dispose (GObject *object)
{
  SysprofSymbolLabel *self = (SysprofSymbolLabel *)object;

  g_clear_pointer (&self->text, gtk_widget_unparent);
  g_clear_object (&self->symbol);

  G_OBJECT_CLASS (sysprof_symbol_label_parent_class)->dispose (object);
}

static void
sysprof_symbol_label_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  SysprofSymbolLabel *self = SYSPROF_SYMBOL_LABEL (object);

  switch (prop_id)
    {
    case PROP_SYMBOL:
      g_value_set_object (value, sysprof_symbol_label_get_symbol (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_symbol_label_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  SysprofSymbolLabel *self = SYSPROF_SYMBOL_LABEL (object);

  switch (prop_id)
    {
    case PROP_SYMBOL:
      sysprof_symbol_label_set_symbol (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_symbol_label_class_init (SysprofSymbolLabelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_symbol_label_dispose;
  object_class->get_property = sysprof_symbol_label_get_property;
  object_class->set_property = sysprof_symbol_label_set_property;

  properties [PROP_SYMBOL] =
    g_param_spec_object ("symbol", NULL, NULL,
                         SYSPROF_TYPE_SYMBOL,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_css_name (widget_class, "symbol");
  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
}

static void
sysprof_symbol_label_init (SysprofSymbolLabel *self)
{
  self->text = g_object_new (GTK_TYPE_INSCRIPTION,
                             "xalign", .0f,
                             "text-overflow", GTK_INSCRIPTION_OVERFLOW_CLIP,
                             NULL);
  gtk_widget_set_parent (GTK_WIDGET (self->text), GTK_WIDGET (self));
}

GtkWidget *
sysprof_symbol_label_new (void)
{
  return g_object_new (SYSPROF_TYPE_SYMBOL_LABEL, NULL);
}

/**
 * sysprof_symbol_label_get_label:
 * @self: a #SysprofSymbolLabel
 *
 * Returns: (transfer none) (nullable): a #SysprofSymbol
 */
SysprofSymbol *
sysprof_symbol_label_get_symbol (SysprofSymbolLabel *self)
{
  g_return_val_if_fail (SYSPROF_IS_SYMBOL_LABEL (self), NULL);

  return self->symbol;
}

void
sysprof_symbol_label_set_symbol (SysprofSymbolLabel *self,
                                 SysprofSymbol      *symbol)
{
  SysprofSymbolKind old_kind;
  SysprofSymbolKind new_kind;
  const char *name;

  g_return_if_fail (SYSPROF_IS_SYMBOL_LABEL (self));
  g_return_if_fail (!symbol || SYSPROF_IS_SYMBOL (symbol));

  if (self->symbol == symbol)
    return;

  if (self->symbol && symbol && _sysprof_symbol_equal (self->symbol, symbol))
    return;

  old_kind = sysprof_symbol_get_kind (self->symbol);
  new_kind = sysprof_symbol_get_kind (symbol);

  g_set_object (&self->symbol, symbol);

  if (old_kind != new_kind)
    {
      if (old_kind > 0)
        gtk_widget_remove_css_class (GTK_WIDGET (self), kind_classes[old_kind]);

      if (new_kind > 0)
        gtk_widget_add_css_class (GTK_WIDGET (self), kind_classes[new_kind]);
    }

  if (symbol != NULL)
    name = sysprof_symbol_get_name (symbol);
  else
    name = NULL;

  gtk_inscription_set_text (GTK_INSCRIPTION (self->text), name);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SYMBOL]);
}
