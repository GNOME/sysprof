/* sysprof-kallsyms-symbolizer.c
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

#include "sysprof-kallsyms-symbolizer.h"
#include "sysprof-document-private.h"
#include "sysprof-symbolizer-private.h"
#include "sysprof-symbol-private.h"

struct _SysprofKallsymsSymbolizer
{
  SysprofSymbolizer parent_instance;
};

struct _SysprofKallsymsSymbolizerClass
{
  SysprofSymbolizerClass parent_class;
};

G_DEFINE_FINAL_TYPE (SysprofKallsymsSymbolizer, sysprof_kallsyms_symbolizer, SYSPROF_TYPE_SYMBOLIZER)

static void
sysprof_kallsyms_symbolizer_prepare_worker (GTask        *task,
                                            gpointer      source_object,
                                            gpointer      task_data,
                                            GCancellable *cancellable)
{
  SysprofKallsymsSymbolizer *self = source_object;
  GDataInputStream *input = task_data;
  g_autoptr(GError) error = NULL;
  char *line;
  gsize len;

  g_assert (G_IS_TASK (task));
  g_assert (SYSPROF_IS_KALLSYMS_SYMBOLIZER (self));
  g_assert (G_IS_DATA_INPUT_STREAM (input));

  while ((line = g_data_input_stream_read_line_utf8 (input, &len, cancellable, &error)))
    {
      /* TODO: port kallsym parser from sysprof-kallsyms.c */

      g_free (line);
    }

  g_task_return_boolean (task, TRUE);
}

static void
sysprof_kallsyms_symbolizer_prepare_async (SysprofSymbolizer   *symbolizer,
                                           SysprofDocument     *document,
                                           GCancellable        *cancellable,
                                           GAsyncReadyCallback  callback,
                                           gpointer             user_data)
{
  SysprofKallsymsSymbolizer *self = (SysprofKallsymsSymbolizer *)symbolizer;
  g_autoptr(SysprofDocumentFile) file = NULL;
  g_autoptr(GZlibDecompressor) decompressor = NULL;
  g_autoptr(GInputStream) input_gz = NULL;
  g_autoptr(GInputStream) input = NULL;
  g_autoptr(GTask) task = NULL;

  g_assert (SYSPROF_IS_KALLSYMS_SYMBOLIZER (self));
  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_kallsyms_symbolizer_prepare_async);

  if (!(file = sysprof_document_lookup_file (document, "/proc/kallsyms.gz")))
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_NOT_SUPPORTED,
                               "No kallsyms found to decode");
      return;
    }

  decompressor = g_zlib_decompressor_new (G_ZLIB_COMPRESSOR_FORMAT_GZIP);
  input_gz = sysprof_document_file_read (file);
  input = g_converter_input_stream_new (input_gz, G_CONVERTER (decompressor));

  g_task_set_task_data (task,
                        g_data_input_stream_new (input),
                        g_object_unref);
  g_task_run_in_thread (task, sysprof_kallsyms_symbolizer_prepare_worker);
}

static gboolean
sysprof_kallsyms_symbolizer_prepare_finish (SysprofSymbolizer  *symbolizer,
                                            GAsyncResult       *result,
                                            GError            **error)
{
  g_assert (SYSPROF_IS_KALLSYMS_SYMBOLIZER (symbolizer));
  g_assert (G_IS_TASK (result));
  g_assert (g_task_is_valid (result, symbolizer));

  return g_task_propagate_boolean (G_TASK (result), error);
}

static SysprofSymbol *
sysprof_kallsyms_symbolizer_symbolize (SysprofSymbolizer        *symbolizer,
                                       SysprofStrings           *strings,
                                       const SysprofProcessInfo *process_info,
                                       SysprofAddress            address)
{
  return NULL;
}

static void
sysprof_kallsyms_symbolizer_finalize (GObject *object)
{
  G_OBJECT_CLASS (sysprof_kallsyms_symbolizer_parent_class)->finalize (object);
}

static void
sysprof_kallsyms_symbolizer_class_init (SysprofKallsymsSymbolizerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofSymbolizerClass *symbolizer_class = SYSPROF_SYMBOLIZER_CLASS (klass);

  object_class->finalize = sysprof_kallsyms_symbolizer_finalize;

  symbolizer_class->prepare_async = sysprof_kallsyms_symbolizer_prepare_async;
  symbolizer_class->prepare_finish = sysprof_kallsyms_symbolizer_prepare_finish;
  symbolizer_class->symbolize = sysprof_kallsyms_symbolizer_symbolize;
}

static void
sysprof_kallsyms_symbolizer_init (SysprofKallsymsSymbolizer *self)
{
}

SysprofSymbolizer *
sysprof_kallsyms_symbolizer_new (void)
{
  return g_object_new (SYSPROF_TYPE_KALLSYMS_SYMBOLIZER, NULL);
}
