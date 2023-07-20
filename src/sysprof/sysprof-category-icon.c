/* sysprof-category-icon.c
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

#include "sysprof-category-icon.h"
#include "sysprof-symbol-private.h"

struct _SysprofCategoryIcon
{
  GtkWidget parent_instance;
  SysprofSymbol *symbol;
  guint category;
};

enum {
  PROP_0,
  PROP_SYMBOL,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofCategoryIcon, sysprof_category_icon, GTK_TYPE_WIDGET)

enum {
  CATEGORY_0,
  CATEGORY_A11Y,
  CATEGORY_ACTIONS,
  CATEGORY_CONSTRUCTORS,
  CATEGORY_CONTEXT_SWITCH,
  CATEGORY_KERNEL,
  CATEGORY_LAYOUT,
  CATEGORY_MAIN_LOOP,
  CATEGORY_PAINT,
  CATEGORY_SIGNALS,
  CATEGORY_TEMPLATES,
  CATEGORY_WINDOWING,
  N_CATEGORIES
};

enum {
  RULE_PREFIX = 1,
  RULE_EXACT,
};

typedef struct _Rule
{
  guint8 kind : 2;
  guint8 inherit : 1;
  guint category;
  const char *match;
} Rule;

typedef struct _RuleGroup
{
  const char *nick;
  const Rule *rules;
} RuleGroup;

static GParamSpec *properties [N_PROPS];
static GdkRGBA category_colors[N_CATEGORIES];
static RuleGroup rule_groups[] = {
  { "EGL",
    (const Rule[]) {
      { RULE_PREFIX, TRUE, CATEGORY_PAINT, "egl" },
      { 0 }
    }
  },

  { "FontConfig",
    (const Rule[]) {
      { RULE_PREFIX, FALSE, CATEGORY_LAYOUT, "" },
      { 0 }
    }
  },

  { "GLib",
    (const Rule[]) {
      { RULE_PREFIX, FALSE,  CATEGORY_MAIN_LOOP, "g_main_loop_" },
      { RULE_PREFIX, FALSE,  CATEGORY_MAIN_LOOP, "g_main_context_" },
      { RULE_PREFIX, FALSE, CATEGORY_MAIN_LOOP, "g_wakeup_" },
      { RULE_EXACT, FALSE,  CATEGORY_MAIN_LOOP, "g_main_dispatch" },
      { 0 }
    }
  },

  { "GObject",
    (const Rule[]) {
      { RULE_PREFIX, FALSE, CATEGORY_SIGNALS, "g_signal_emit" },
      { RULE_PREFIX, FALSE, CATEGORY_SIGNALS, "g_signal_emit" },
      { RULE_PREFIX, FALSE, CATEGORY_SIGNALS, "g_object_notify" },
      { RULE_PREFIX, FALSE, CATEGORY_CONSTRUCTORS, "g_object_new" },
      { RULE_PREFIX, FALSE, CATEGORY_CONSTRUCTORS, "g_type_create_instance" },
      { 0 }
    }
  },

  { "GTK 4",
    (const Rule[]) {
      { RULE_PREFIX, TRUE, CATEGORY_LAYOUT, "gtk_css_" },
      { RULE_PREFIX, TRUE, CATEGORY_LAYOUT, "gtk_widget_measure" },
      { RULE_PREFIX, TRUE, CATEGORY_PAINT, "gdk_snapshot" },
      { RULE_PREFIX, TRUE, CATEGORY_PAINT, "gtk_snapshot" },
      { RULE_PREFIX, TRUE, CATEGORY_LAYOUT, "gtk_widget_reposition" },
      { RULE_PREFIX, TRUE, CATEGORY_WINDOWING, "gtk_window_present" },
      { RULE_PREFIX, TRUE, CATEGORY_ACTIONS, "gtk_action_muxer_" },
      { RULE_PREFIX, TRUE, CATEGORY_A11Y, "gtk_accessible_" },
      { RULE_PREFIX, TRUE, CATEGORY_A11Y, "gtk_at_" },
      { RULE_PREFIX, TRUE, CATEGORY_TEMPLATES, "gtk_builder_" },
      { RULE_PREFIX, TRUE, CATEGORY_TEMPLATES, "gtk_buildable_" },
      { RULE_PREFIX, TRUE, CATEGORY_LAYOUT, "gtk_widget_root" },
      { 0 }
    }
  },

  { "libc",
    (const Rule[]) {
      { RULE_EXACT, TRUE, CATEGORY_MAIN_LOOP, "poll" },
      { 0 }
    }
  },

  { "Pango",
    (const Rule[]) {
      { RULE_PREFIX, FALSE, CATEGORY_LAYOUT, "" },
      { 0 }
    }
  },

  { "Wayland Client",
    (const Rule[]) {
      { RULE_PREFIX, TRUE, CATEGORY_WINDOWING, "wl_" },
      { 0 }
    }
  },

  { "Wayland Server",
    (const Rule[]) {
      { RULE_PREFIX, TRUE, CATEGORY_WINDOWING, "wl_" },
      { 0 }
    }
  },
};

static guint
categorize_symbol (SysprofSymbol *symbol)
{
  if (symbol->kind == SYSPROF_SYMBOL_KIND_KERNEL)
    return CATEGORY_KERNEL;
  else if (symbol->kind == SYSPROF_SYMBOL_KIND_CONTEXT_SWITCH)
    return CATEGORY_CONTEXT_SWITCH;
  else if (symbol->kind != SYSPROF_SYMBOL_KIND_USER ||
           symbol->binary_nick == NULL)
    return CATEGORY_0;

  for (guint i = 0; i < G_N_ELEMENTS (rule_groups); i++)
    {
      if (strcmp (rule_groups[i].nick, symbol->binary_nick) != 0)
        continue;

      for (guint j = 0; rule_groups[i].rules[j].kind; j++)
        {
          if (rule_groups[i].rules[j].kind == RULE_PREFIX)
            {
              if (g_str_has_prefix (symbol->name, rule_groups[i].rules[j].match))
                return rule_groups[i].rules[j].category;
            }
          else if (rule_groups[i].rules[j].kind == RULE_EXACT)
            {
              if (strcmp (symbol->name, rule_groups[i].rules[j].match) == 0)
                return rule_groups[i].rules[j].category;
            }
        }

      break;
    }

  return CATEGORY_0;
}

static void
sysprof_category_icon_snapshot (GtkWidget   *widget,
                                GtkSnapshot *snapshot)
{
  SysprofCategoryIcon *self = (SysprofCategoryIcon *)widget;
  const GdkRGBA *color = NULL;

  g_assert (SYSPROF_IS_CATEGORY_ICON (self));
  g_assert (GTK_IS_SNAPSHOT (snapshot));

  color = &category_colors[self->category];

  if (color->alpha == 0)
    return;

  gtk_snapshot_append_color (snapshot,
                             color,
                             &GRAPHENE_RECT_INIT (0, 0,
                                                  gtk_widget_get_width (widget),
                                                  gtk_widget_get_height (widget)));
}

static void
sysprof_category_icon_finalize (GObject *object)
{
  SysprofCategoryIcon *self = (SysprofCategoryIcon *)object;

  g_clear_object (&self->symbol);

  G_OBJECT_CLASS (sysprof_category_icon_parent_class)->finalize (object);
}

static void
sysprof_category_icon_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  SysprofCategoryIcon *self = SYSPROF_CATEGORY_ICON (object);

  switch (prop_id)
    {
    case PROP_SYMBOL:
      g_value_set_object (value, sysprof_category_icon_get_symbol (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_category_icon_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  SysprofCategoryIcon *self = SYSPROF_CATEGORY_ICON (object);

  switch (prop_id)
    {
    case PROP_SYMBOL:
      sysprof_category_icon_set_symbol (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_category_icon_class_init (SysprofCategoryIconClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = sysprof_category_icon_finalize;
  object_class->get_property = sysprof_category_icon_get_property;
  object_class->set_property = sysprof_category_icon_set_property;

  widget_class->snapshot = sysprof_category_icon_snapshot;

  properties[PROP_SYMBOL] =
    g_param_spec_object ("symbol", NULL, NULL,
                         SYSPROF_TYPE_SYMBOL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gdk_rgba_parse (&category_colors[CATEGORY_A11Y], "#000");
  gdk_rgba_parse (&category_colors[CATEGORY_ACTIONS], "#f66151");
  gdk_rgba_parse (&category_colors[CATEGORY_CONSTRUCTORS], "#613583");
  gdk_rgba_parse (&category_colors[CATEGORY_CONSTRUCTORS], "#62a0ea");
  gdk_rgba_parse (&category_colors[CATEGORY_CONTEXT_SWITCH], "#ffbe6f");
  gdk_rgba_parse (&category_colors[CATEGORY_KERNEL], "#a51d2d");
  gdk_rgba_parse (&category_colors[CATEGORY_LAYOUT], "#9141ac");
  gdk_rgba_parse (&category_colors[CATEGORY_MAIN_LOOP], "#5e5c64");
  gdk_rgba_parse (&category_colors[CATEGORY_PAINT], "#2ec27e");
  gdk_rgba_parse (&category_colors[CATEGORY_SIGNALS], "#e5a50a");
  gdk_rgba_parse (&category_colors[CATEGORY_TEMPLATES], "#77767b");
  gdk_rgba_parse (&category_colors[CATEGORY_WINDOWING], "#c64600");
}

static void
sysprof_category_icon_init (SysprofCategoryIcon *self)
{
}

SysprofSymbol *
sysprof_category_icon_get_symbol (SysprofCategoryIcon *self)
{
  g_return_val_if_fail (SYSPROF_IS_CATEGORY_ICON (self), NULL);

  return self->symbol;
}

void
sysprof_category_icon_set_symbol (SysprofCategoryIcon *self,
                                  SysprofSymbol       *symbol)
{
  g_return_if_fail (SYSPROF_IS_CATEGORY_ICON (self));

  if (g_set_object (&self->symbol, symbol))
    {
      self->category = symbol ? categorize_symbol (symbol) : 0;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SYMBOL]);
      gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}
