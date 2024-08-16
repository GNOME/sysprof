/* sysprof-jitmap-symbolizer.c
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

#include <stdlib.h>

#include "timsort/gtktimsortprivate.h"

#include "sysprof-document-jitmap.h"
#include "sysprof-document-private.h"
#include "sysprof-jitmap-symbolizer.h"
#include "sysprof-symbol-private.h"
#include "sysprof-symbolizer-private.h"

typedef struct _Jitmap
{
  SysprofAddress address;
  GRefString *name;
} Jitmap;

struct _SysprofJitmapSymbolizer
{
  SysprofSymbolizer parent_instance;
  GArray *jitmaps;
};

struct _SysprofJitmapSymbolizerClass
{
  SysprofSymbolizerClass parent_class;
};

G_DEFINE_FINAL_TYPE (SysprofJitmapSymbolizer, sysprof_jitmap_symbolizer, SYSPROF_TYPE_SYMBOLIZER)

typedef struct
{
  SysprofDocument *document;
  GListModel *model;
} Prepare;

static void
prepare_free (gpointer data)
{
  Prepare *prepare = data;

  g_clear_object (&prepare->model);
  g_clear_object (&prepare->document);
  g_free (prepare);
}

static void
jitmap_clear (gpointer data)
{
  Jitmap *j = data;
  g_clear_pointer (&j->name, g_ref_string_release);
}

static int
compare_by_address (gconstpointer a,
                    gconstpointer b)
{
  const Jitmap *jitmap_a = a;
  const Jitmap *jitmap_b = b;

  if (jitmap_a->address < jitmap_b->address)
    return -1;
  else if (jitmap_a->address > jitmap_b->address)
    return 1;
  else
    return 0;
}

static void
sysprof_jitmap_symbolizer_prepare_worker (GTask        *task,
                                          gpointer      source_object,
                                          gpointer      task_data,
                                          GCancellable *cancellable)
{
  SysprofJitmapSymbolizer *self = source_object;
  Prepare *prepare = task_data;
  guint n_jitmaps;

  g_assert (G_IS_TASK (task));
  g_assert (SYSPROF_IS_JITMAP_SYMBOLIZER (self));
  g_assert (prepare != NULL);
  g_assert (SYSPROF_IS_DOCUMENT (prepare->document));
  g_assert (G_IS_LIST_MODEL (prepare->model));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  n_jitmaps = g_list_model_get_n_items (prepare->model);

  for (guint i = 0; i < n_jitmaps; i++)
    {
      g_autoptr(SysprofDocumentJitmap) jitmap = g_list_model_get_item (prepare->model, i);
      guint size = sysprof_document_jitmap_get_size (jitmap);

      for (guint j = 0; j < size; j++)
        {
          const char *name;
          Jitmap map;

          if (!(name = sysprof_document_jitmap_get_mapping (jitmap, j, &map.address)))
            continue;

          map.name = _sysprof_document_ref_string (prepare->document, name);
          g_array_append_val (self->jitmaps, map);
        }
    }

  gtk_tim_sort (self->jitmaps->data,
                self->jitmaps->len,
                sizeof (Jitmap),
                (GCompareDataFunc)compare_by_address,
                NULL);

  g_task_return_boolean (task, TRUE);
}

static void
sysprof_jitmap_symbolizer_prepare_async (SysprofSymbolizer   *symbolizer,
                                         SysprofDocument     *document,
                                         GCancellable        *cancellable,
                                         GAsyncReadyCallback  callback,
                                         gpointer             user_data)
{
  SysprofJitmapSymbolizer *self = (SysprofJitmapSymbolizer *)symbolizer;
  g_autoptr(GTask) task = NULL;
  Prepare *prepare;

  g_assert (SYSPROF_IS_JITMAP_SYMBOLIZER (self));
  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  prepare = g_new0 (Prepare, 1);
  prepare->document = g_object_ref (document);
  prepare->model = sysprof_document_list_jitmaps (document);

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_jitmap_symbolizer_prepare_async);
  g_task_set_task_data (task, prepare, prepare_free);
  g_task_run_in_thread (task, sysprof_jitmap_symbolizer_prepare_worker);
}

static gboolean
sysprof_jitmap_symbolizer_prepare_finish (SysprofSymbolizer  *symbolizer,
                                          GAsyncResult       *result,
                                          GError            **error)
{
  g_assert (SYSPROF_IS_JITMAP_SYMBOLIZER (symbolizer));
  g_assert (G_IS_TASK (result));
  g_assert (g_task_is_valid (result, symbolizer));

  return g_task_propagate_boolean (G_TASK (result), error);
}

static SysprofSymbol *
sysprof_jitmap_symbolizer_symbolize (SysprofSymbolizer        *symbolizer,
                                     SysprofStrings           *strings,
                                     const SysprofProcessInfo *process_info,
                                     SysprofAddressContext     context,
                                     SysprofAddress            address)
{
  SysprofJitmapSymbolizer *self = (SysprofJitmapSymbolizer *)symbolizer;
  const Jitmap key = { address, NULL };
  guint guess = (address & 0xFFFF) - 1;
  const Jitmap *match;

  if (context != SYSPROF_ADDRESS_CONTEXT_NONE &&
      context != SYSPROF_ADDRESS_CONTEXT_USER)
    return NULL;

  if ((address & 0xFFFFFFFF00000000) != 0xE000000000000000)
    return NULL;

  /* Jitmap addresses generally start at 1 and work their way up
   * monotonically (after masking off the high 0xE...............
   * bits). So we can try for a fast index lookup to skip any sort
   * of searching in the well behaved case.
   */
  if G_LIKELY (guess < self->jitmaps->len)
    {
      match = &g_array_index (self->jitmaps, Jitmap, guess);

      if G_LIKELY (match->address == address)
        goto create_symbol;
    }

  match = bsearch (&key,
                   self->jitmaps->data,
                   self->jitmaps->len,
                   sizeof (Jitmap),
                   compare_by_address);

  if (match == NULL)
    return NULL;

create_symbol:
  return _sysprof_symbol_new (g_ref_string_acquire (match->name),
                              NULL,
                              NULL,
                              match->address,
                              match->address + 1,
                              SYSPROF_SYMBOL_KIND_USER);
}

static void
sysprof_jitmap_symbolizer_finalize (GObject *object)
{
  SysprofJitmapSymbolizer *self = (SysprofJitmapSymbolizer *)object;

  g_clear_pointer (&self->jitmaps, g_array_unref);

  G_OBJECT_CLASS (sysprof_jitmap_symbolizer_parent_class)->finalize (object);
}

static void
sysprof_jitmap_symbolizer_class_init (SysprofJitmapSymbolizerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofSymbolizerClass *symbolizer_class = SYSPROF_SYMBOLIZER_CLASS (klass);

  object_class->finalize = sysprof_jitmap_symbolizer_finalize;

  symbolizer_class->prepare_async = sysprof_jitmap_symbolizer_prepare_async;
  symbolizer_class->prepare_finish = sysprof_jitmap_symbolizer_prepare_finish;
  symbolizer_class->symbolize = sysprof_jitmap_symbolizer_symbolize;
}

static void
sysprof_jitmap_symbolizer_init (SysprofJitmapSymbolizer *self)
{
  self->jitmaps = g_array_new (FALSE, FALSE, sizeof (Jitmap));
  g_array_set_clear_func (self->jitmaps, jitmap_clear);
}

SysprofSymbolizer *
sysprof_jitmap_symbolizer_new (void)
{
  return g_object_new (SYSPROF_TYPE_JITMAP_SYMBOLIZER, NULL);
}
