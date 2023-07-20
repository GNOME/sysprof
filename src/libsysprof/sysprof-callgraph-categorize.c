/* sysprof-callgraph-categorize.c
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

#include "sysprof-callgraph-private.h"
#include "sysprof-symbol-private.h"

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

static RuleGroup rule_groups[] = {
  { "EGL",
    (const Rule[]) {
      { RULE_PREFIX, TRUE, SYSPROF_CALLGRAPH_CATEGORY_PAINT, "egl" },
      { 0 }
    }
  },

  { "FontConfig",
    (const Rule[]) {
      { RULE_PREFIX, FALSE, SYSPROF_CALLGRAPH_CATEGORY_LAYOUT, "" },
      { 0 }
    }
  },

  { "GLib",
    (const Rule[]) {
      { RULE_PREFIX, FALSE,  SYSPROF_CALLGRAPH_CATEGORY_MAIN_LOOP, "g_main_loop_" },
      { RULE_PREFIX, FALSE,  SYSPROF_CALLGRAPH_CATEGORY_MAIN_LOOP, "g_main_context_" },
      { RULE_PREFIX, FALSE, SYSPROF_CALLGRAPH_CATEGORY_MAIN_LOOP, "g_wakeup_" },
      { RULE_EXACT, FALSE,  SYSPROF_CALLGRAPH_CATEGORY_MAIN_LOOP, "g_main_dispatch" },
      { 0 }
    }
  },

  { "GObject",
    (const Rule[]) {
      { RULE_PREFIX, FALSE, SYSPROF_CALLGRAPH_CATEGORY_SIGNALS, "g_signal_emit" },
      { RULE_PREFIX, FALSE, SYSPROF_CALLGRAPH_CATEGORY_SIGNALS, "g_signal_emit" },
      { RULE_PREFIX, FALSE, SYSPROF_CALLGRAPH_CATEGORY_SIGNALS, "g_object_notify" },
      { RULE_PREFIX, FALSE, SYSPROF_CALLGRAPH_CATEGORY_CONSTRUCTORS, "g_object_new" },
      { RULE_PREFIX, FALSE, SYSPROF_CALLGRAPH_CATEGORY_CONSTRUCTORS, "g_type_create_instance" },
      { 0 }
    }
  },

  { "GTK 4",
    (const Rule[]) {
      { RULE_PREFIX, TRUE, SYSPROF_CALLGRAPH_CATEGORY_LAYOUT, "gtk_css_" },
      { RULE_PREFIX, TRUE, SYSPROF_CALLGRAPH_CATEGORY_LAYOUT, "gtk_widget_measure" },
      { RULE_PREFIX, TRUE, SYSPROF_CALLGRAPH_CATEGORY_PAINT, "gdk_snapshot" },
      { RULE_PREFIX, TRUE, SYSPROF_CALLGRAPH_CATEGORY_PAINT, "gtk_snapshot" },
      { RULE_PREFIX, TRUE, SYSPROF_CALLGRAPH_CATEGORY_LAYOUT, "gtk_widget_reposition" },
      { RULE_PREFIX, TRUE, SYSPROF_CALLGRAPH_CATEGORY_WINDOWING, "gtk_window_present" },
      { RULE_PREFIX, TRUE, SYSPROF_CALLGRAPH_CATEGORY_ACTIONS, "gtk_action_muxer_" },
      { RULE_PREFIX, TRUE, SYSPROF_CALLGRAPH_CATEGORY_A11Y, "gtk_accessible_" },
      { RULE_PREFIX, TRUE, SYSPROF_CALLGRAPH_CATEGORY_A11Y, "gtk_at_" },
      { RULE_PREFIX, TRUE, SYSPROF_CALLGRAPH_CATEGORY_TEMPLATES, "gtk_builder_" },
      { RULE_PREFIX, TRUE, SYSPROF_CALLGRAPH_CATEGORY_TEMPLATES, "gtk_buildable_" },
      { RULE_PREFIX, TRUE, SYSPROF_CALLGRAPH_CATEGORY_LAYOUT, "gtk_widget_root" },
      { RULE_PREFIX, TRUE, SYSPROF_CALLGRAPH_CATEGORY_PAINT, "gdk_frame_clock_paint" },
      { RULE_PREFIX, TRUE, SYSPROF_CALLGRAPH_CATEGORY_LAYOUT, "_gdk_frame_clock_emit_layout" },
      { RULE_PREFIX, TRUE, SYSPROF_CALLGRAPH_CATEGORY_PAINT, "_gdk_frame_clock_emit_paint" },
      { RULE_PREFIX, TRUE, SYSPROF_CALLGRAPH_CATEGORY_PAINT, "_gdk_frame_clock_emit_after_paint" },
      { RULE_PREFIX, TRUE, SYSPROF_CALLGRAPH_CATEGORY_INPUT, "gdk_surface_handle_event" },
      { 0 }
    }
  },

  { "libc",
    (const Rule[]) {
      { RULE_EXACT, TRUE, SYSPROF_CALLGRAPH_CATEGORY_MAIN_LOOP, "poll" },
      { 0 }
    }
  },

  { "Pango",
    (const Rule[]) {
      { RULE_PREFIX, FALSE, SYSPROF_CALLGRAPH_CATEGORY_LAYOUT, "" },
      { 0 }
    }
  },

  { "Wayland Client",
    (const Rule[]) {
      { RULE_PREFIX, TRUE, SYSPROF_CALLGRAPH_CATEGORY_WINDOWING, "wl_" },
      { 0 }
    }
  },

  { "Wayland Server",
    (const Rule[]) {
      { RULE_PREFIX, TRUE, SYSPROF_CALLGRAPH_CATEGORY_WINDOWING, "wl_" },
      { 0 }
    }
  },
};


SysprofCallgraphCategory
_sysprof_callgraph_node_categorize (SysprofCallgraphNode *node)
{
  SysprofSymbol *symbol;

  g_return_val_if_fail (node, SYSPROF_CALLGRAPH_CATEGORY_UNCATEGORIZED);
  g_return_val_if_fail (node->summary, SYSPROF_CALLGRAPH_CATEGORY_UNCATEGORIZED);
  g_return_val_if_fail (node->summary->symbol, SYSPROF_CALLGRAPH_CATEGORY_UNCATEGORIZED);

  symbol = node->summary->symbol;

  /* NOTE: We could certainly extend this to allow users to define custom
   * categorization. Additionally, we might want to match more than one node so
   * that you can do valgrind style function matches like:
   */

  if (symbol->kind != SYSPROF_SYMBOL_KIND_USER ||
      symbol->binary_nick == NULL)
    return SYSPROF_CALLGRAPH_CATEGORY_UNCATEGORIZED;

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

  return SYSPROF_CALLGRAPH_CATEGORY_UNCATEGORIZED;
}
