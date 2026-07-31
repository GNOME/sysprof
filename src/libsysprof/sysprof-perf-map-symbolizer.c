/* sysprof-perf-map-symbolizer.c
 *
 * Copyright 2026 Christian Hergert
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

#include <errno.h>

#include "timsort/gtktimsortprivate.h"

#include "sysprof-document-file.h"
#include "sysprof-document-private.h"
#include "sysprof-perf-map-symbolizer.h"
#include "sysprof-process-info-private.h"
#include "sysprof-symbol-private.h"
#include "sysprof-symbolizer-private.h"

#define PERF_MAP_PREFIX "__perf_maps__/"

typedef struct _PerfMapEntry
{
  SysprofAddress begin;
  SysprofAddress end;
  GRefString *name;
} PerfMapEntry;

struct _SysprofPerfMapSymbolizer
{
  SysprofSymbolizer parent_instance;
  GHashTable *maps_by_pid;
  GRefString *binary_nick;
};

struct _SysprofPerfMapSymbolizerClass
{
  SysprofSymbolizerClass parent_class;
};

typedef struct _Prepare
{
  SysprofDocument *document;
  GListModel *files;
} Prepare;

G_DEFINE_FINAL_TYPE (SysprofPerfMapSymbolizer,
                     sysprof_perf_map_symbolizer,
                     SYSPROF_TYPE_SYMBOLIZER)

static void
perf_map_entry_clear (gpointer data)
{
  PerfMapEntry *entry = data;

  g_clear_pointer (&entry->name, g_ref_string_release);
}

static void
prepare_free (gpointer data)
{
  Prepare *prepare = data;

  g_clear_object (&prepare->document);
  g_clear_object (&prepare->files);
  g_free (prepare);
}

static int
compare_by_begin (gconstpointer a,
                  gconstpointer b)
{
  const PerfMapEntry *entry_a = a;
  const PerfMapEntry *entry_b = b;

  if (entry_a->begin < entry_b->begin)
    return -1;
  else if (entry_a->begin > entry_b->begin)
    return 1;
  else
    return 0;
}

static gboolean
parse_pid (const char *path,
           int        *pid)
{
  const char *number;
  char *endptr = NULL;
  gint64 value;

  g_assert (path != NULL);
  g_assert (pid != NULL);

  if (!g_str_has_prefix (path, PERF_MAP_PREFIX))
    return FALSE;

  number = path + strlen (PERF_MAP_PREFIX);
  errno = 0;
  value = g_ascii_strtoll (number, &endptr, 10);

  if (errno != 0 || value <= 0 || value > G_MAXINT ||
      endptr == number || !g_str_equal (endptr, ".map"))
    return FALSE;

  *pid = value;

  return TRUE;
}

static gboolean
parse_line (SysprofDocument *document,
            char            *line,
            PerfMapEntry    *entry)
{
  char *begin_end = NULL;
  char *size_end = NULL;
  char *name;
  guint64 begin;
  guint64 size;

  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (line != NULL);
  g_assert (entry != NULL);

  errno = 0;
  begin = g_ascii_strtoull (line, &begin_end, 16);
  if (errno != 0 || begin_end == line || !g_ascii_isspace (*begin_end))
    return FALSE;

  while (g_ascii_isspace (*begin_end))
    begin_end++;

  errno = 0;
  size = g_ascii_strtoull (begin_end, &size_end, 16);
  if (errno != 0 || size_end == begin_end || !g_ascii_isspace (*size_end) ||
      size == 0 || size > G_MAXUINT64 - begin)
    return FALSE;

  while (g_ascii_isspace (*size_end))
    size_end++;

  name = size_end;
  if (*name == 0)
    return FALSE;

  if (name[strlen (name) - 1] == '\r')
    name[strlen (name) - 1] = 0;

  if (*name == 0)
    return FALSE;

  entry->begin = begin;
  entry->end = begin + size;
  entry->name = _sysprof_document_ref_string (document, name);

  return TRUE;
}

static void
parse_file (SysprofPerfMapSymbolizer *self,
            SysprofDocument          *document,
            SysprofDocumentFile      *file,
            int                       pid)
{
  g_autoptr(GBytes) bytes = NULL;
  g_autofree char *contents = NULL;
  g_auto(GStrv) lines = NULL;
  GArray *entries;
  gsize len;

  g_assert (SYSPROF_IS_PERF_MAP_SYMBOLIZER (self));
  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (SYSPROF_IS_DOCUMENT_FILE (file));
  g_assert (pid > 0);

  bytes = sysprof_document_file_dup_bytes (file);
  contents = g_strndup (g_bytes_get_data (bytes, NULL), g_bytes_get_size (bytes));
  lines = g_strsplit (contents, "\n", 0);

  entries = g_array_new (FALSE, FALSE, sizeof (PerfMapEntry));
  g_array_set_clear_func (entries, perf_map_entry_clear);

  for (guint i = 0; lines[i] != NULL; i++)
    {
      PerfMapEntry entry = {0};

      if (parse_line (document, lines[i], &entry))
        g_array_append_val (entries, entry);
    }

  len = entries->len;
  if (len == 0)
    {
      g_array_unref (entries);
      return;
    }

  gtk_tim_sort (entries->data,
                entries->len,
                sizeof (PerfMapEntry),
                (GCompareDataFunc)compare_by_begin,
                NULL);

  g_hash_table_replace (self->maps_by_pid, GINT_TO_POINTER (pid), entries);
}

static void
sysprof_perf_map_symbolizer_prepare_worker (GTask        *task,
                                            gpointer      source_object,
                                            gpointer      task_data,
                                            GCancellable *cancellable)
{
  SysprofPerfMapSymbolizer *self = source_object;
  Prepare *prepare = task_data;
  guint n_items;

  g_assert (G_IS_TASK (task));
  g_assert (SYSPROF_IS_PERF_MAP_SYMBOLIZER (self));
  g_assert (prepare != NULL);
  g_assert (SYSPROF_IS_DOCUMENT (prepare->document));
  g_assert (G_IS_LIST_MODEL (prepare->files));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  n_items = g_list_model_get_n_items (prepare->files);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(SysprofDocumentFile) file = g_list_model_get_item (prepare->files, i);
      const char *path = sysprof_document_file_get_path (file);
      int pid;

      if (parse_pid (path, &pid))
        parse_file (self, prepare->document, file, pid);
    }

  g_task_return_boolean (task, TRUE);
}

static void
sysprof_perf_map_symbolizer_prepare_async (SysprofSymbolizer   *symbolizer,
                                           SysprofDocument     *document,
                                           GCancellable        *cancellable,
                                           GAsyncReadyCallback  callback,
                                           gpointer             user_data)
{
  SysprofPerfMapSymbolizer *self = (SysprofPerfMapSymbolizer *)symbolizer;
  g_autoptr(GTask) task = NULL;
  Prepare *prepare;

  g_assert (SYSPROF_IS_PERF_MAP_SYMBOLIZER (self));
  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  prepare = g_new0 (Prepare, 1);
  prepare->document = g_object_ref (document);
  prepare->files = sysprof_document_list_files (document);

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_perf_map_symbolizer_prepare_async);
  g_task_set_task_data (task, prepare, prepare_free);
  g_task_run_in_thread (task, sysprof_perf_map_symbolizer_prepare_worker);
}

static gboolean
sysprof_perf_map_symbolizer_prepare_finish (SysprofSymbolizer  *symbolizer,
                                            GAsyncResult       *result,
                                            GError            **error)
{
  g_assert (SYSPROF_IS_PERF_MAP_SYMBOLIZER (symbolizer));
  g_assert (G_IS_TASK (result));
  g_assert (g_task_is_valid (result, symbolizer));

  return g_task_propagate_boolean (G_TASK (result), error);
}

static SysprofSymbol *
sysprof_perf_map_symbolizer_symbolize (SysprofSymbolizer        *symbolizer,
                                       SysprofStrings           *strings,
                                       const SysprofProcessInfo *process_info,
                                       SysprofAddressContext     context,
                                       SysprofAddress            address)
{
  SysprofPerfMapSymbolizer *self = (SysprofPerfMapSymbolizer *)symbolizer;
  GArray *entries;
  guint left;
  guint right;

  g_assert (SYSPROF_IS_PERF_MAP_SYMBOLIZER (self));

  if (process_info == NULL ||
      (context != SYSPROF_ADDRESS_CONTEXT_NONE &&
       context != SYSPROF_ADDRESS_CONTEXT_USER) ||
      !(entries = g_hash_table_lookup (self->maps_by_pid,
                                      GINT_TO_POINTER (process_info->pid))))
    return NULL;

  left = 0;
  right = entries->len;

  while (left < right)
    {
      guint middle = left + ((right - left) / 2);
      const PerfMapEntry *entry = &g_array_index (entries, PerfMapEntry, middle);

      if (entry->begin <= address)
        left = middle + 1;
      else
        right = middle;
    }

  if (left > 0)
    {
      const PerfMapEntry *entry = &g_array_index (entries, PerfMapEntry, left - 1);

      if (address < entry->end)
        return _sysprof_symbol_new (g_ref_string_acquire (entry->name),
                                    NULL,
                                    g_ref_string_acquire (self->binary_nick),
                                    entry->begin,
                                    entry->end,
                                    SYSPROF_SYMBOL_KIND_USER);
    }

  return NULL;
}

static void
sysprof_perf_map_symbolizer_finalize (GObject *object)
{
  SysprofPerfMapSymbolizer *self = (SysprofPerfMapSymbolizer *)object;

  g_clear_pointer (&self->maps_by_pid, g_hash_table_unref);
  g_clear_pointer (&self->binary_nick, g_ref_string_release);

  G_OBJECT_CLASS (sysprof_perf_map_symbolizer_parent_class)->finalize (object);
}

static void
sysprof_perf_map_symbolizer_class_init (SysprofPerfMapSymbolizerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofSymbolizerClass *symbolizer_class = SYSPROF_SYMBOLIZER_CLASS (klass);

  object_class->finalize = sysprof_perf_map_symbolizer_finalize;

  symbolizer_class->prepare_async = sysprof_perf_map_symbolizer_prepare_async;
  symbolizer_class->prepare_finish = sysprof_perf_map_symbolizer_prepare_finish;
  symbolizer_class->symbolize = sysprof_perf_map_symbolizer_symbolize;
}

static void
sysprof_perf_map_symbolizer_init (SysprofPerfMapSymbolizer *self)
{
  self->maps_by_pid = g_hash_table_new_full (NULL,
                                             NULL,
                                             NULL,
                                             (GDestroyNotify)g_array_unref);
  self->binary_nick = g_ref_string_new ("JIT");
}

/**
 * sysprof_perf_map_symbolizer_new:
 *
 * Creates a symbolizer for perf-map files embedded by [class@Sysprof.PerfMap].
 *
 * Returns: (transfer full): a new #SysprofPerfMapSymbolizer
 */
SysprofSymbolizer *
sysprof_perf_map_symbolizer_new (void)
{
  return g_object_new (SYSPROF_TYPE_PERF_MAP_SYMBOLIZER, NULL);
}
