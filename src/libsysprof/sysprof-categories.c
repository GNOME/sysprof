/* sysprof-categories.c
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

#include "libsysprof-resources.h"

#include "sysprof-callgraph-private.h"
#include "sysprof-categories-private.h"
#include "sysprof-enums.h"

#include "line-reader-private.h"

struct _SysprofCategories
{
  GHashTable *binary_nick_to_rules;
};

enum {
  MATCH_EXACT,
  MATCH_PREFIX,
  MATCH_SUFFIX,
};

typedef struct _Rule
{
  guint8 kind : 2;
  guint8 inherit : 1;
  guint8 category : 5;
  char *match;
} Rule;

static SysprofCallgraphCategory
parse_category (const char *category)
{
  static GEnumClass *enum_class;
  const GEnumValue *value;

  if (enum_class == NULL)
    enum_class = g_type_class_ref (SYSPROF_TYPE_CALLGRAPH_CATEGORY);

  if (!(value = g_enum_get_value_by_nick (enum_class, category)))
    return 0;

  return value->value;
}

static void
clear_rule (gpointer element)
{
  Rule *rule = element;

  g_clear_pointer (&rule->match, g_free);
}

static void
sysprof_categories_add_rule (SysprofCategories        *categories,
                             const char               *binary_nick,
                             const char               *match,
                             SysprofCallgraphCategory  category,
                             gboolean                  inherit)
{
  Rule rule;
  GArray *ar;

  g_assert (categories != NULL);
  g_assert (binary_nick != NULL);
  g_assert (match != NULL);

  if (match[0] == 0)
    return;

  if G_UNLIKELY (!(ar = g_hash_table_lookup (categories->binary_nick_to_rules, binary_nick)))
    {
      ar = g_array_new (FALSE, FALSE, sizeof (Rule));
      g_array_set_clear_func (ar, clear_rule);
      g_hash_table_insert (categories->binary_nick_to_rules,
                           g_strdup (binary_nick),
                           ar);
    }

  if (match[0] == '*')
    {
      rule.kind = MATCH_SUFFIX;
      rule.match = g_strdup (&match[1]);
      rule.category = category;
      rule.inherit = inherit;

      g_array_append_val (ar, rule);
    }
  else if (match[strlen(match)-1] == '*')
    {
      rule.kind = MATCH_PREFIX;
      rule.match = g_strndup (match, strlen (match)-1);
      rule.category = category;
      rule.inherit = inherit;

      g_array_append_val (ar, rule);
    }
  else
    {
      rule.kind = MATCH_EXACT;
      rule.match = g_strdup (match);
      rule.category = category;
      rule.inherit = inherit;

      g_array_append_val (ar, rule);
    }
}

SysprofCategories *
sysprof_categories_new (void)
{
  SysprofCategories *categories;
  g_autoptr(GBytes) bytes = NULL;
  g_autofree char *binary_nick = NULL;
  const char *str;
  const char *lineptr;
  gsize line_len;
  gsize len;
  guint lineno = 0;
  LineReader reader;

  g_resources_register (libsysprof_get_resource ());

  if (!(bytes = g_resources_lookup_data ("/org/gnome/libsysprof/categories.txt", 0, NULL)))
    return NULL;

  if (!(str = (const char *)g_bytes_get_data (bytes, &len)))
    return NULL;

  line_reader_init (&reader, (char *)str, len);

  categories = g_new0 (SysprofCategories, 1);
  categories->binary_nick_to_rules = g_hash_table_new_full (g_str_hash,
                                                            g_str_equal,
                                                            g_free,
                                                            (GDestroyNotify)g_array_unref);

  while ((lineptr = line_reader_next (&reader, &line_len)))
    {
      g_autofree char *line = g_strndup (lineptr, line_len);
      SysprofCallgraphCategory category_value;
      g_auto(GStrv) parts = NULL;
      const char *match = NULL;
      const char *category = NULL;
      const char *inherit = NULL;

      lineno++;
      g_strstrip (line);

      /* # starts comment line */
      if (line[0] == 0 || line[0] == '#')
        continue;

      /* Group lines look like "binary nick:\n" */
      if (g_str_has_suffix (line, ":"))
        {
          line[line_len-1] = 0;
          g_set_str (&binary_nick, g_strstrip (line));
          continue;
        }

      parts = g_strsplit (line, " ", 0);

      for (guint i = 0; parts[i]; i++)
        {
          g_strstrip (parts[i]);

          if (parts[i][0] == 0)
            continue;

          if (match == NULL)
            {
              match = parts[i];
              continue;
            }

          if (category == NULL)
            {
              category = parts[i];
              continue;
            }

          if (inherit == NULL)
            {
              inherit = parts[i];
              continue;
            }

          break;
        }

      if (match == NULL || category == NULL)
        {
          g_warning ("categories.txt: line: %d: Incomplete rule", lineno);
          continue;
        }

      if (inherit && !g_str_equal (inherit, "inherit"))
        {
          g_warning ("categories.txt line %d: malformated inherit", lineno);
          continue;
        }

      if (!(category_value = parse_category (category)))
        {
          g_warning ("categories.txt line %d: malformated category", lineno);
          continue;
        }

      sysprof_categories_add_rule (categories, binary_nick, match, category_value, !!inherit);
    }

  return categories;
}

void
sysprof_categories_free (SysprofCategories *categories)
{
  if (categories != NULL)
    {
      g_clear_pointer (&categories->binary_nick_to_rules, g_hash_table_unref);
      g_free (categories);
    }
}

SysprofCallgraphCategory
sysprof_categories_lookup (SysprofCategories *categories,
                           const char        *binary_nick,
                           const char        *symbol)
{
  GArray *rules;

  if (categories == NULL)
    categories = sysprof_categories_get_default ();

  if (binary_nick == NULL || symbol == NULL)
    return 0;

  if (!(rules = g_hash_table_lookup (categories->binary_nick_to_rules, binary_nick)))
    return 0;

  for (guint i = 0; i < rules->len; i++)
    {
      const Rule *rule = &g_array_index (rules, Rule, i);
      gboolean ret;

      if (rule->kind == MATCH_EXACT)
        ret = strcmp (rule->match, symbol) == 0;
      else if (rule->kind == MATCH_PREFIX)
        ret = g_str_has_prefix (symbol, rule->match);
      else if (rule->kind == MATCH_SUFFIX)
        ret = g_str_has_suffix (symbol, rule->match);
      else
        ret = FALSE;

      if (ret)
        {
          if (rule->inherit)
            return rule->category | SYSPROF_CALLGRAPH_CATEGORY_INHERIT;

          return rule->category;
        }
    }

  return 0;
}

SysprofCategories *
sysprof_categories_get_default (void)
{
  static SysprofCategories *instance;

  if (instance == NULL)
    instance = sysprof_categories_new ();

  return instance;
}
