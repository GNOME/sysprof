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

#include "sysprof-symbols-source.h"

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

static void
sysprof_symbols_source_supplement (SysprofSource        *source,
                                   SysprofCaptureReader *reader)
{
  SysprofSymbolsSource *self = (SysprofSymbolsSource *)source;

  g_assert (SYSPROF_IS_SYMBOLS_SOURCE (self));
  g_assert (reader != NULL);
  g_assert (self->writer != NULL);


}

static void
source_iface_init (SysprofSourceInterface *iface)
{
  iface->set_writer = sysprof_symbols_source_set_writer;
  iface->supplement = sysprof_symbols_source_supplement;
}
