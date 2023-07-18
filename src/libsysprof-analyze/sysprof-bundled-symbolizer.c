/* sysprof-bundled-symbolizer.c
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

#include "sysprof-bundled-symbolizer-private.h"
#include "sysprof-document-private.h"
#include "sysprof-symbolizer-private.h"
#include "sysprof-symbol-private.h"

struct _SysprofBundledSymbolizer
{
  SysprofSymbolizer          parent_instance;

  const SysprofPackedSymbol *symbols;
  guint                      n_symbols;

  GBytes                    *bytes;
  const gchar               *beginptr;
  const gchar               *endptr;
};

struct _SysprofBundledSymbolizerClass
{
  SysprofSymbolizerClass parent_class;
};

G_DEFINE_FINAL_TYPE (SysprofBundledSymbolizer, sysprof_bundled_symbolizer, SYSPROF_TYPE_SYMBOLIZER)

static void
sysprof_bundled_symbolizer_decode (SysprofBundledSymbolizer *self,
                                   GBytes                   *bytes,
                                   gboolean                  is_native)
{
  char *beginptr;
  char *endptr;

  g_assert (SYSPROF_IS_BUNDLED_SYMBOLIZER (self));
  g_assert (bytes != NULL);

  /* Our GBytes always contain a trialing \0 after what we think
   * is the end of the buffer.
   */
  beginptr = (char *)g_bytes_get_data (bytes, NULL);
  endptr = beginptr + g_bytes_get_size (bytes);

  for (char *ptr = beginptr;
       ptr < endptr && (ptr + sizeof (SysprofPackedSymbol)) < endptr;
       ptr += sizeof (SysprofPackedSymbol))
    {
      SysprofPackedSymbol *sym = (SysprofPackedSymbol *)ptr;

      if (sym->addr_begin == 0 &&
          sym->addr_end == 0 &&
          sym->pid == 0 &&
          sym->offset == 0)
        {
          self->symbols = (const SysprofPackedSymbol *)beginptr;
          self->n_symbols = sym - self->symbols;
          break;
        }
      else if (!is_native)
        {
          sym->addr_begin = GUINT64_SWAP_LE_BE (sym->addr_begin);
          sym->addr_end = GUINT64_SWAP_LE_BE (sym->addr_end);
          sym->pid = GUINT32_SWAP_LE_BE (sym->pid);
          sym->offset = GUINT32_SWAP_LE_BE (sym->offset);
          sym->tag_offset = GUINT32_SWAP_LE_BE (sym->tag_offset);
        }
    }

  self->beginptr = beginptr;
  self->endptr = endptr;
  self->bytes = g_bytes_ref (bytes);
}

static void
sysprof_bundled_symbolizer_prepare_async (SysprofSymbolizer   *symbolizer,
                                          SysprofDocument     *document,
                                          GCancellable        *cancellable,
                                          GAsyncReadyCallback  callback,
                                          gpointer             user_data)
{
  SysprofBundledSymbolizer *self = (SysprofBundledSymbolizer *)symbolizer;
  g_autoptr(SysprofDocumentFile) file = NULL;
  g_autoptr(GBytes) bytes = NULL;
  g_autoptr(GTask) task = NULL;

  g_assert (SYSPROF_IS_BUNDLED_SYMBOLIZER (self));
  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_bundled_symbolizer_prepare_async);

  if ((file = sysprof_document_lookup_file (document, "__symbols__")) &&
      (bytes = sysprof_document_file_dup_bytes (file)))
    sysprof_bundled_symbolizer_decode (self, bytes, _sysprof_document_is_native (document));

  g_task_return_boolean (task, TRUE);
}

static gboolean
sysprof_bundled_symbolizer_prepare_finish (SysprofSymbolizer  *symbolizer,
                                           GAsyncResult       *result,
                                           GError            **error)
{
  g_assert (SYSPROF_IS_BUNDLED_SYMBOLIZER (symbolizer));
  g_assert (G_IS_TASK (result));
  g_assert (g_task_is_valid (result, symbolizer));

  return g_task_propagate_boolean (G_TASK (result), error);
}

static gint
search_for_symbol_cb (gconstpointer a,
                      gconstpointer b)
{
  const SysprofPackedSymbol *key = a;
  const SysprofPackedSymbol *ele = b;

  if (key->pid < ele->pid)
    return -1;

  if (key->pid > ele->pid)
    return 1;

  g_assert (key->pid == ele->pid);

  if (key->addr_begin < ele->addr_begin)
    return -1;

  if (key->addr_begin >= ele->addr_end)
    return 1;

  g_assert (key->addr_begin >= ele->addr_begin);
  g_assert (key->addr_end <= ele->addr_end);

  return 0;
}

static SysprofSymbol *
sysprof_bundled_symbolizer_symbolize (SysprofSymbolizer        *symbolizer,
                                      SysprofStrings           *strings,
                                      const SysprofProcessInfo *process_info,
                                      SysprofAddressContext     context,
                                      SysprofAddress            address)
{
  SysprofBundledSymbolizer *self = SYSPROF_BUNDLED_SYMBOLIZER (symbolizer);
  g_autoptr(GRefString) tag = NULL;
  const SysprofPackedSymbol *ret;
  const SysprofPackedSymbol key = {
    .addr_begin = address,
    .addr_end = address,
    .pid = process_info ? process_info->pid : 0,
    .offset = 0,
    .tag_offset = 0,
  };

  if (self->n_symbols == 0)
    return NULL;

  g_assert (self->symbols != NULL);
  g_assert (self->n_symbols > 0);

  ret = bsearch (&key,
                 self->symbols,
                 self->n_symbols,
                 sizeof (SysprofPackedSymbol),
                 search_for_symbol_cb);

  if (ret == NULL || ret->offset == 0)
    return NULL;

  if (ret->tag_offset > 0)
    {
      if (ret->tag_offset < (self->endptr - self->beginptr))
        tag = sysprof_strings_get (strings, &self->beginptr[ret->tag_offset]);
    }

  if (ret->offset < (self->endptr - self->beginptr))
    {
      const char *name = &self->beginptr[ret->offset];
      SysprofSymbolKind kind;

      if (g_str_has_prefix (name, "- -"))
        kind = SYSPROF_SYMBOL_KIND_CONTEXT_SWITCH;
      else if (context == SYSPROF_ADDRESS_CONTEXT_KERNEL)
        kind = SYSPROF_SYMBOL_KIND_KERNEL;
      else if (name[0] == '[')
        kind = SYSPROF_SYMBOL_KIND_PROCESS;
      else
        kind = SYSPROF_SYMBOL_KIND_USER;

      return _sysprof_symbol_new (sysprof_strings_get (strings, name),
                                  NULL,
                                  g_steal_pointer (&tag),
                                  ret->addr_begin,
                                  ret->addr_end,
                                  kind);
    }

  return NULL;
}

static void
sysprof_bundled_symbolizer_finalize (GObject *object)
{
  SysprofBundledSymbolizer *self = (SysprofBundledSymbolizer *)object;

  self->symbols = NULL;
  self->n_symbols = 0;
  self->beginptr = NULL;
  self->endptr = NULL;

  g_clear_pointer (&self->bytes, g_bytes_unref);

  G_OBJECT_CLASS (sysprof_bundled_symbolizer_parent_class)->finalize (object);
}

static void
sysprof_bundled_symbolizer_class_init (SysprofBundledSymbolizerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofSymbolizerClass *symbolizer_class = SYSPROF_SYMBOLIZER_CLASS (klass);

  object_class->finalize = sysprof_bundled_symbolizer_finalize;

  symbolizer_class->prepare_async = sysprof_bundled_symbolizer_prepare_async;
  symbolizer_class->prepare_finish = sysprof_bundled_symbolizer_prepare_finish;
  symbolizer_class->symbolize = sysprof_bundled_symbolizer_symbolize;
}

static void
sysprof_bundled_symbolizer_init (SysprofBundledSymbolizer *self)
{
}

SysprofSymbolizer *
sysprof_bundled_symbolizer_new (void)
{
  return g_object_new (SYSPROF_TYPE_BUNDLED_SYMBOLIZER, NULL);
}
