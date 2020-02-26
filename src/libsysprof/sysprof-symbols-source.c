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
#include "sysprof-private.h"
#include "sysprof-symbol-map.h"
#include "sysprof-symbols-source.h"

#include "sysprof-platform.h"

struct _SysprofSymbolsSource
{
  GObject               parent_instance;
  SysprofCaptureWriter *writer;
  guint                 user_only : 1;
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

static void
sysprof_symbols_source_supplement (SysprofSource        *source,
                                   SysprofCaptureReader *reader)
{
  SysprofSymbolsSource *self = (SysprofSymbolsSource *)source;
  g_autoptr(SysprofSymbolResolver) native = NULL;
  g_autoptr(SysprofSymbolResolver) kernel = NULL;
  SysprofSymbolMap *map;
  gint fd;

  g_assert (SYSPROF_IS_SYMBOLS_SOURCE (self));
  g_assert (reader != NULL);
  g_assert (self->writer != NULL);

  if (-1 == (fd = sysprof_memfd_create ("[sysprof-decode]")))
    return;

  map = sysprof_symbol_map_new ();

  native = sysprof_elf_symbol_resolver_new ();
  sysprof_symbol_map_add_resolver (map, native);

  if (!self->user_only)
    {
      kernel = sysprof_kernel_symbol_resolver_new ();
      sysprof_symbol_map_add_resolver (map, kernel);
    }

  sysprof_symbol_map_resolve (map, reader);
  sysprof_symbol_map_serialize (map, fd);
  sysprof_symbol_map_free (map);

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

void
sysprof_symbols_source_set_user_only (SysprofSymbolsSource *self,
                                      gboolean              user_only)
{
  g_return_if_fail (SYSPROF_IS_SYMBOLS_SOURCE (self));

  self->user_only = !!user_only;
}

gboolean
sysprof_symbols_source_get_user_only (SysprofSymbolsSource *self)
{
  g_return_val_if_fail (SYSPROF_IS_SYMBOLS_SOURCE (self), FALSE);

  return self->user_only;
}
