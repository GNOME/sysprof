/* sysprof-symbols-source.c
 *
 * Copyright 2019 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sysprof-symbols-source"

#include "config.h"

#include "sysprof-elf-symbol-resolver.h"
#include "sysprof-symbols-source.h"

#include "sysprof-platform.h"

struct _SysprofSymbolsSource
{
  GObject               parent_instance;
  SysprofCaptureWriter *writer;
};

static void source_iface_init (SysprofSourceInterface *iface);

G_DEFINE_TYPE_WITH_CODE (SysprofSymbolsSource, sysprof_symbols_source, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (SYSPROF_TYPE_SOURCE, source_iface_init))

static void
sysprof_symbols_source_finalize (GObject *object)
{
  SysprofSymbolsSource *self = (SysprofSymbolsSource *)object;

  g_clear_pointer (&self->writer, sysprof_capture_writer_unref);

  G_OBJECT_CLASS (sysprof_symbols_source_parent_class)->finalize (object);
}

static void
sysprof_symbols_source_class_init (SysprofSymbolsSourceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_symbols_source_finalize;
}

static void
sysprof_symbols_source_init (SysprofSymbolsSource *self)
{
}

static void
sysprof_symbols_source_set_writer (SysprofSource        *source,
                                   SysprofCaptureWriter *writer)
{
  SysprofSymbolsSource *self = (SysprofSymbolsSource *)source;

  g_assert (SYSPROF_IS_SYMBOLS_SOURCE (self));
  g_assert (writer != NULL);

  g_clear_pointer (&self->writer, sysprof_capture_writer_unref);
  self->writer = sysprof_capture_writer_ref (writer);
}

SYSPROF_ALIGNED_BEGIN(1)
typedef struct
{
  SysprofCaptureAddress  begin;
  SysprofCaptureAddress  end;
  guint32                pid;
  guint32                offset;
  guint32                tag_offset;
  guint32                padding;
} Symbol
SYSPROF_ALIGNED_END(1);

G_STATIC_ASSERT (sizeof (Symbol) == 32);

static void
symbol_free (Symbol *sym)
{
  g_slice_free (Symbol, sym);
}

static gint
symbol_compare (gconstpointer          a,
                gconstpointer          b,
                G_GNUC_UNUSED gpointer unused)
{
  const Symbol *aa = a;
  const Symbol *bb = b;

  if (aa->pid < bb->pid)
    return -1;

  if (aa->pid > bb->pid)
    return 1;

  if (aa->begin < bb->begin)
    return -1;

  if (aa->begin > bb->begin)
    return -1;

  return 0;
}

static void
sysprof_symbols_source_supplement (SysprofSource        *source,
                                   SysprofCaptureReader *reader)
{
  SysprofSymbolsSource *self = (SysprofSymbolsSource *)source;
  g_autoptr(SysprofSymbolResolver) native = NULL;
  g_autoptr(GByteArray) strings = NULL;
  g_autoptr(GHashTable) seen = NULL;
  g_autoptr(GSequence) symbols = NULL;
  SysprofCaptureFrameType type;
  guint base;
  gint fd = -1;

  g_assert (SYSPROF_IS_SYMBOLS_SOURCE (self));
  g_assert (reader != NULL);
  g_assert (self->writer != NULL);

  if (-1 == (fd = sysprof_memfd_create ("[sysprof-decode]")))
    return;

  strings = g_byte_array_new ();
  symbols = g_sequence_new ((GDestroyNotify) symbol_free);
  seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  native = sysprof_elf_symbol_resolver_new ();
  sysprof_symbol_resolver_load (native, reader);
  sysprof_capture_reader_reset (reader);

  while (sysprof_capture_reader_peek_type (reader, &type))
    {
      SysprofAddressContext last_context = SYSPROF_ADDRESS_CONTEXT_NONE;
      const SysprofCaptureSample *sample;

      if (type != SYSPROF_CAPTURE_FRAME_SAMPLE)
        {
          sysprof_capture_reader_skip (reader);
          continue;
        }

      sample = sysprof_capture_reader_read_sample (reader);

      for (guint i = 0; i < sample->n_addrs; i++)
        {
          g_autofree gchar *str = NULL;
          SysprofCaptureAddress addr = sample->addrs[i];
          SysprofCaptureAddress begin, end;
          SysprofAddressContext context;
          GSequenceIter *iter;
          const gchar *qstr;
          Symbol sym;
          GQuark tag = 0;
          gpointer poff;

          if (sysprof_address_is_context_switch (addr, &context))
            {
              last_context = context;
              continue;
            }

          if (!sysprof_elf_symbol_resolver_resolve_full ((SysprofElfSymbolResolver *)native,
                                                         sample->frame.time,
                                                         sample->frame.pid,
                                                         last_context,
                                                         addr,
                                                         &begin,
                                                         &end,
                                                         &str,
                                                         &tag))
            continue;

          g_assert (str != NULL);

          sym.pid = sample->frame.pid;
          sym.begin = begin;
          sym.end = end;
          sym.offset = 0;
          sym.tag_offset = 0;

          /* Lookup (but get us the next position on miss so we can insert
           * without a second lookup for the same position.
           */
          iter = g_sequence_search (symbols, &sym, symbol_compare, NULL);

          /* Skip if we found it */
          if (!g_sequence_iter_is_end (iter) &&
              symbol_compare (&sym, g_sequence_get (iter), NULL) == 0)
            continue;

          /* Add the string if we haven't seen it */
          if (!g_hash_table_lookup_extended (seen, str, NULL, &poff))
            {
              sym.offset = strings->len;
              g_byte_array_append (strings, (guint8 *)str, strlen (str) + 1);
              g_hash_table_insert (seen, g_steal_pointer (&str), GUINT_TO_POINTER (sym.offset));
            }
          else
            {
              sym.offset = GPOINTER_TO_UINT (poff);
            }

          /* And the tag */
          if (tag != 0 &&
              (qstr = g_quark_to_string (tag)) &&
              !g_hash_table_lookup_extended (seen, qstr, NULL, &poff))
            {
              sym.tag_offset = strings->len;
              g_byte_array_append (strings, (guint8 *)qstr, strlen (qstr) + 1);
              g_hash_table_insert (seen, g_strdup (qstr), GUINT_TO_POINTER (sym.tag_offset));
            }
          else
            {
              sym.tag_offset = GPOINTER_TO_UINT (poff);
            }

          g_sequence_insert_before (iter, g_slice_dup (Symbol, &sym));
        }
    }

  g_debug ("Writing supplemental %d symbols with %u bytes of strings",
           g_sequence_get_length (symbols),
           strings->len);

  base = (g_sequence_get_length (symbols) + 1) * sizeof (Symbol);

  for (GSequenceIter *iter = g_sequence_get_begin_iter (symbols);
       !g_sequence_iter_is_end (iter);
       iter = g_sequence_iter_next (iter))
    {
      Symbol *sym = g_sequence_get (iter);

      sym->offset += base;
      sym->tag_offset += base;

      write (fd, sym, sizeof *sym);
    }

  /* Write tailing empty symbol */
    {
      static const Symbol zero = {0};
      write (fd, &zero, sizeof zero);
    }

  write (fd, strings->data, strings->len);
  lseek (fd, 0, SEEK_SET);

  sysprof_capture_writer_add_file_fd (self->writer,
                                      SYSPROF_CAPTURE_CURRENT_TIME,
                                      -1,
                                      -1,
                                      "__symbols__",
                                      fd);

  close (fd);
}

static void
sysprof_symbols_source_start (SysprofSource *source)
{
  g_assert (SYSPROF_IS_SYMBOLS_SOURCE (source));

  sysprof_source_emit_ready (source);
}

static void
sysprof_symbols_source_stop (SysprofSource *source)
{
  g_assert (SYSPROF_IS_SYMBOLS_SOURCE (source));

  sysprof_source_emit_finished (source);
}

static void
source_iface_init (SysprofSourceInterface *iface)
{
  iface->set_writer = sysprof_symbols_source_set_writer;
  iface->supplement = sysprof_symbols_source_supplement;
  iface->start = sysprof_symbols_source_start;
  iface->stop = sysprof_symbols_source_stop;
}

SysprofSource *
sysprof_symbols_source_new (void)
{
  return g_object_new (SYSPROF_TYPE_SYMBOLS_SOURCE, NULL);
}
