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

#include <errno.h>

#include "timsort/gtktimsortprivate.h"

#include "rust-demangle.h"

#include "sysprof-kallsyms-symbolizer.h"
#include "sysprof-document-private.h"
#include "sysprof-strings-private.h"
#include "sysprof-symbolizer-private.h"
#include "sysprof-symbol-private.h"

#define LAST_SYMBOL_LEN 0xffff

typedef struct _KernelSymbol
{
  guint64     address;
  GRefString *name;
} KernelSymbol;

struct _SysprofKallsymsSymbolizer
{
  SysprofSymbolizer  parent_instance;
  GInputStream      *stream;
  GArray            *kallsyms;
  guint64            low;
  guint64            high;
};

struct _SysprofKallsymsSymbolizerClass
{
  SysprofSymbolizerClass parent_class;
};

G_DEFINE_FINAL_TYPE (SysprofKallsymsSymbolizer, sysprof_kallsyms_symbolizer, SYSPROF_TYPE_SYMBOLIZER)

static SysprofStrings *kallsym_strings;
static GRefString *linux_string;

static void
kernel_symbol_clear (gpointer data)
{
  KernelSymbol *symbol = data;

  g_clear_pointer (&symbol->name, g_ref_string_release);
}

static inline void
sysprof_kallsyms_symbolizer_add (SysprofKallsymsSymbolizer *self,
                                 guint64                    address,
                                 guint8                     type,
                                 const char                *name)
{
  const KernelSymbol s = { address, sysprof_strings_get (kallsym_strings, name) };
  g_array_append_val (self->kallsyms, s);
}

static inline int
sort_by_address (gconstpointer a,
                 gconstpointer b)
{
  const KernelSymbol *sym_a = a;
  const KernelSymbol *sym_b = b;

  if (sym_a->address < sym_b->address)
    return -1;
  else if (sym_a->address > sym_b->address)
    return 1;
  else
    return 0;
}

static void
sysprof_kallsyms_symbolizer_prepare_worker (GTask        *task,
                                            gpointer      source_object,
                                            gpointer      task_data,
                                            GCancellable *cancellable)
{
  SysprofKallsymsSymbolizer *self = source_object;
  GDataInputStream *input = task_data;
  g_autoptr(GError) error = NULL;
  guint64 last_address = 0;
  char *line;
  gsize len;

  g_assert (G_IS_TASK (task));
  g_assert (SYSPROF_IS_KALLSYMS_SYMBOLIZER (self));
  g_assert (G_IS_DATA_INPUT_STREAM (input));

  while ((line = g_data_input_stream_read_line_utf8 (input, &len, cancellable, &error)))
    {
      const char *endptr = &line[len];
      const char *name;
      guint64 address;
      char *iter = line;
      guint8 type;

      address = g_ascii_strtoull (iter, &iter, 16);

      if G_UNLIKELY ((address == 0 && errno == EINVAL) ||
                     (address == G_MAXUINT64 && errno == ERANGE))
        goto failure;

      if G_UNLIKELY (iter[0] != ' ')
        goto failure;

      /* Swallow space */
      iter++;
      if (iter >= endptr)
        goto failure;

      /* Get type 'ABDRTVWabdrtw' */
      type = iter[0];

      /* Move past type and space */
      iter++;
      iter++;
      if (iter >= endptr)
        goto failure;

      /* Name starts here */
      name = iter;

      /* Walk ahead to first space or \0 */
      while (iter < endptr && !g_ascii_isspace (*iter))
        iter++;
      if (iter > endptr)
        goto failure;

      /* Make @name usable as C string */
      *iter = 0;

      /* Sometimes we get duplicates in kallsyms right after one another.
       * Rather than try to deduplicate those all after they're in the
       * array just detect the simple case and skip them now.
       */
      if (address != last_address)
        {
          g_autofree char *demangle = NULL;

          /* If we got a Rust kernel symbol, demangle it */
          if (name[0] == '_' && name[1] == 'R' && name[2] == 'N')
            name = demangle = sysprof_rust_demangle (name, 0);

          sysprof_kallsyms_symbolizer_add (self, address, type, name);
        }

      last_address = address;

    failure:
      g_free (line);
    }

  /* We cannot rely on sorting of kallsyms up-front from Linux in all
   * cases so we must sort the resulting array now.
   */
  gtk_tim_sort (self->kallsyms->data,
                self->kallsyms->len,
                sizeof (KernelSymbol),
                (GCompareDataFunc)sort_by_address,
                NULL);

  /* Store a "best guess" at an lower/upper bound for the max address so that
   * we can avoid searching for anything unreasonably past the end of the last
   * kernel symbol.
   */
  if (self->kallsyms->len > 0)
    {
      const KernelSymbol *head = &g_array_index (self->kallsyms, KernelSymbol, 0);
      const KernelSymbol *tail = &g_array_index (self->kallsyms, KernelSymbol, self->kallsyms->len-1);

      self->low = head->address;
      self->high = tail->address + LAST_SYMBOL_LEN;
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
  g_autoptr(GInputStream) input = NULL;
  g_autoptr(GTask) task = NULL;
  GInputStream *base_stream;

  g_assert (SYSPROF_IS_KALLSYMS_SYMBOLIZER (self));
  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_kallsyms_symbolizer_prepare_async);

  base_stream = self->stream;

  if (base_stream == NULL)
    {
      if (!(file = sysprof_document_lookup_file (document, "/proc/kallsyms")))
        {
          g_task_return_new_error (task,
                                   G_IO_ERROR,
                                   G_IO_ERROR_NOT_SUPPORTED,
                                   "No kallsyms found to decode");
          return;
        }

      base_stream = input = sysprof_document_file_read (file);
    }

  g_assert (base_stream != NULL);
  g_assert (G_IS_INPUT_STREAM (base_stream));

  g_task_set_task_data (task,
                        g_data_input_stream_new (base_stream),
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
                                       SysprofAddressContext     context,
                                       SysprofAddress            address)
{
  SysprofKallsymsSymbolizer *self = (SysprofKallsymsSymbolizer *)symbolizer;
  const KernelSymbol *symbols;
  guint n_symbols;
  guint left;
  guint right;
  guint mid;

  if (context != SYSPROF_ADDRESS_CONTEXT_KERNEL)
    return NULL;

  if (address < self->low || address >= self->high)
    goto failure;

  symbols = &g_array_index (self->kallsyms, KernelSymbol, 0);
  n_symbols = self->kallsyms->len;

  left = 0;
  right = n_symbols;
  mid = n_symbols / 2;

  while (left <= right)
    {
      const KernelSymbol *ksym = &symbols[mid];
      const KernelSymbol *next = &symbols[mid+1];

      if (address >= ksym->address &&
          (address < next->address || next->address == 0))
        return _sysprof_symbol_new (g_ref_string_acquire (ksym->name),
                                    NULL,
                                    g_ref_string_acquire (linux_string),
                                    ksym->address,
                                    next->address ? next->address : ksym->address + LAST_SYMBOL_LEN,
                                    SYSPROF_SYMBOL_KIND_KERNEL);

      if (address < ksym->address)
        right = mid;
      else
        left = mid + 1;

      mid = left + ((right-left) / 2);
    }

failure:
  {
    SysprofSymbol *ret;
    char name[64];

    g_snprintf (name, sizeof name, "In Kernel+0x%"G_GINT64_MODIFIER"x", address);
    ret = _sysprof_symbol_new (sysprof_strings_get (strings, name),
                               NULL,
                               g_ref_string_acquire (linux_string),
                               address,
                               address + 1,
                               SYSPROF_SYMBOL_KIND_KERNEL);
    ret->is_fallback = TRUE;

    return ret;
  }
}

static void
sysprof_kallsyms_symbolizer_finalize (GObject *object)
{
  SysprofKallsymsSymbolizer *self = (SysprofKallsymsSymbolizer *)object;

  g_clear_object (&self->stream);
  g_clear_pointer (&self->kallsyms, g_array_unref);

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

  kallsym_strings = sysprof_strings_new ();
  linux_string = g_ref_string_new_intern ("Linux");
}

static void
sysprof_kallsyms_symbolizer_init (SysprofKallsymsSymbolizer *self)
{
  self->kallsyms = g_array_new (TRUE, FALSE, sizeof (KernelSymbol));
  g_array_set_clear_func (self->kallsyms, kernel_symbol_clear);
}

SysprofSymbolizer *
sysprof_kallsyms_symbolizer_new (void)
{
  return g_object_new (SYSPROF_TYPE_KALLSYMS_SYMBOLIZER, NULL);
}

/**
 * sysprof_kallsyms_symbolizer_new_for_symbols:
 * @symbols: (transfer full): a #GInputStream
 *
 * Creates a symbolizer using the contents of @symbols as the contents
 * of `/proc/kallsyms` for decoding. This is useful if you need to unwind
 * symbols from a machine that is different than where the capture was
 * recorded and have not embedded `/proc/kallsyms.gz` within the capture
 * file for use by #SysprofKallsymsSymbolizer.
 *
 * Returns: (transfer full): a #SysprofKallsymsSymbolizer
 */
SysprofSymbolizer *
sysprof_kallsyms_symbolizer_new_for_symbols (GInputStream *symbols)
{
  SysprofKallsymsSymbolizer *self;

  g_return_val_if_fail (G_IS_INPUT_STREAM (symbols), NULL);

  self = g_object_new (SYSPROF_TYPE_KALLSYMS_SYMBOLIZER, NULL);
  self->stream = symbols;

  return SYSPROF_SYMBOLIZER (self);
}
