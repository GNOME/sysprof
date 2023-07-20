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
  guint8                    kind : 2;
  SysprofCallgraphCategory  category;
  const char               *match;
} Rule;

typedef struct _RuleGroup
{
  const char *nick;
  const Rule *rules;
} RuleGroup;

static RuleGroup rule_groups[] = {
  { "EGL",
    (const Rule[]) {
      { RULE_PREFIX, SYSPROF_CALLGRAPH_CATEGORY_PAINT|SYSPROF_CALLGRAPH_CATEGORY_INHERIT, "egl" },
      { 0 }
    }
  },

  { "FontConfig",
    (const Rule[]) {
      { RULE_PREFIX, SYSPROF_CALLGRAPH_CATEGORY_LAYOUT|SYSPROF_CALLGRAPH_CATEGORY_INHERIT, "" },
      { 0 }
    }
  },

  { "GLib",
    (const Rule[]) {
      { RULE_PREFIX, SYSPROF_CALLGRAPH_CATEGORY_MAIN_LOOP, "g_main_loop_" },
      { RULE_PREFIX, SYSPROF_CALLGRAPH_CATEGORY_MAIN_LOOP, "g_main_context_" },
      { RULE_PREFIX, SYSPROF_CALLGRAPH_CATEGORY_MAIN_LOOP|SYSPROF_CALLGRAPH_CATEGORY_INHERIT, "g_wakeup_" },
      { RULE_EXACT, SYSPROF_CALLGRAPH_CATEGORY_MAIN_LOOP, "g_main_dispatch" },
      { 0 }
    }
  },

  { "GTK 4",
    (const Rule[]) {
      { RULE_PREFIX, SYSPROF_CALLGRAPH_CATEGORY_LAYOUT|SYSPROF_CALLGRAPH_CATEGORY_INHERIT, "gtk_css_" },
      { RULE_PREFIX, SYSPROF_CALLGRAPH_CATEGORY_LAYOUT|SYSPROF_CALLGRAPH_CATEGORY_INHERIT, "gtk_widget_measure" },
      { RULE_PREFIX, SYSPROF_CALLGRAPH_CATEGORY_PAINT|SYSPROF_CALLGRAPH_CATEGORY_INHERIT, "gdk_snapshot" },
      { RULE_PREFIX, SYSPROF_CALLGRAPH_CATEGORY_PAINT|SYSPROF_CALLGRAPH_CATEGORY_INHERIT, "gtk_snapshot" },
      { RULE_PREFIX, SYSPROF_CALLGRAPH_CATEGORY_LAYOUT|SYSPROF_CALLGRAPH_CATEGORY_INHERIT, "gtk_widget_reposition" },
      { RULE_PREFIX, SYSPROF_CALLGRAPH_CATEGORY_WINDOWING|SYSPROF_CALLGRAPH_CATEGORY_INHERIT, "gtk_window_present" },
      { RULE_PREFIX, SYSPROF_CALLGRAPH_CATEGORY_ACTIONS|SYSPROF_CALLGRAPH_CATEGORY_INHERIT, "gtk_action_muxer_" },
      { RULE_PREFIX, SYSPROF_CALLGRAPH_CATEGORY_A11Y|SYSPROF_CALLGRAPH_CATEGORY_INHERIT, "gtk_accessible_" },
      { RULE_PREFIX, SYSPROF_CALLGRAPH_CATEGORY_A11Y|SYSPROF_CALLGRAPH_CATEGORY_INHERIT, "gtk_at_" },
      { RULE_PREFIX, SYSPROF_CALLGRAPH_CATEGORY_LAYOUT|SYSPROF_CALLGRAPH_CATEGORY_INHERIT, "gtk_builder_" },
      { RULE_PREFIX, SYSPROF_CALLGRAPH_CATEGORY_LAYOUT|SYSPROF_CALLGRAPH_CATEGORY_INHERIT, "gtk_buildable_" },
      { RULE_PREFIX, SYSPROF_CALLGRAPH_CATEGORY_LAYOUT|SYSPROF_CALLGRAPH_CATEGORY_INHERIT, "gtk_widget_root" },
      { RULE_PREFIX, SYSPROF_CALLGRAPH_CATEGORY_PAINT|SYSPROF_CALLGRAPH_CATEGORY_INHERIT, "gdk_frame_clock_paint" },
      { RULE_PREFIX, SYSPROF_CALLGRAPH_CATEGORY_LAYOUT|SYSPROF_CALLGRAPH_CATEGORY_INHERIT, "_gdk_frame_clock_emit_layout" },
      { RULE_PREFIX, SYSPROF_CALLGRAPH_CATEGORY_PAINT|SYSPROF_CALLGRAPH_CATEGORY_INHERIT, "_gdk_frame_clock_emit_paint" },
      { RULE_PREFIX, SYSPROF_CALLGRAPH_CATEGORY_PAINT|SYSPROF_CALLGRAPH_CATEGORY_INHERIT, "_gdk_frame_clock_emit_after_paint" },
      { RULE_PREFIX, SYSPROF_CALLGRAPH_CATEGORY_INPUT|SYSPROF_CALLGRAPH_CATEGORY_INHERIT, "gdk_surface_handle_event" },
      { RULE_EXACT, SYSPROF_CALLGRAPH_CATEGORY_PAINT|SYSPROF_CALLGRAPH_CATEGORY_INHERIT, "gdk_surface_paint_on_clock" },
      { RULE_EXACT, SYSPROF_CALLGRAPH_CATEGORY_PAINT|SYSPROF_CALLGRAPH_CATEGORY_INHERIT, "gdk_widget_render" },
      { RULE_EXACT, SYSPROF_CALLGRAPH_CATEGORY_PAINT|SYSPROF_CALLGRAPH_CATEGORY_INHERIT, "gdk_widget_real_snapshot" },
      { RULE_PREFIX, SYSPROF_CALLGRAPH_CATEGORY_PAINT|SYSPROF_CALLGRAPH_CATEGORY_INHERIT, "gsk_gl_" },
      { 0 }
    }
  },

  { "libc",
    (const Rule[]) {
      { RULE_EXACT, SYSPROF_CALLGRAPH_CATEGORY_MAIN_LOOP|SYSPROF_CALLGRAPH_CATEGORY_INHERIT, "poll" },
      { 0 }
    }
  },

  { "Pango",
    (const Rule[]) {
      { RULE_PREFIX, SYSPROF_CALLGRAPH_CATEGORY_LAYOUT|SYSPROF_CALLGRAPH_CATEGORY_INHERIT, "" },
      { 0 }
    }
  },

  { "Wayland Client",
    (const Rule[]) {
      { RULE_PREFIX, SYSPROF_CALLGRAPH_CATEGORY_WINDOWING|SYSPROF_CALLGRAPH_CATEGORY_INHERIT, "wl_" },
      { 0 }
    }
  },

  { "Wayland Server",
    (const Rule[]) {
      { RULE_PREFIX, SYSPROF_CALLGRAPH_CATEGORY_WINDOWING|SYSPROF_CALLGRAPH_CATEGORY_INHERIT, "wl_" },
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
