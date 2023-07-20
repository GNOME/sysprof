/* sysprof-symbols-bundle.c
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

#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <sysprof.h>

#include "sysprof-instrument-private.h"
#include "sysprof-recording-private.h"
#include "sysprof-symbols-bundle.h"

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureReader, sysprof_capture_reader_unref)

static const DexAsyncPairInfo load_info =
  DEX_ASYNC_PAIR_INFO_OBJECT (sysprof_document_loader_load_async,
                              sysprof_document_loader_load_finish);

struct _SysprofSymbolsBundle
{
  SysprofInstrument parent_instance;
};

struct _SysprofSymbolsBundleClass
{
  SysprofInstrumentClass parent_class;
};

G_DEFINE_FINAL_TYPE (SysprofSymbolsBundle, sysprof_symbols_bundle, SYSPROF_TYPE_INSTRUMENT)

static DexFuture *
sysprof_symbols_bundle_augment_fiber (gpointer user_data)
{
  const DexAsyncPairInfo serialize_info = DEX_ASYNC_PAIR_INFO_BOXED (
    sysprof_document_serialize_symbols_async,
    sysprof_document_serialize_symbols_finish,
    G_TYPE_BYTES);

  g_autoptr(SysprofDocumentLoader) loader = NULL;
  g_autoptr(SysprofSymbolizer) elf = NULL;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(GBytes) bytes = NULL;
  SysprofRecording *recording = user_data;
  g_autoptr(GError) error = NULL;
  g_autofd int fd = -1;

  g_assert (SYSPROF_IS_RECORDING (recording));

  if (-1 == (fd = sysprof_recording_dup_fd (recording)))
    return dex_future_new_for_errno (errno);
  g_assert (fd > -1);

  if (!(loader = sysprof_document_loader_new_for_fd (fd, &error)))
    return dex_future_new_for_error (g_steal_pointer (&error));
  g_assert (SYSPROF_IS_DOCUMENT_LOADER (loader));

  /* Only symbolize ELF symbols as the rest can be symbolized
   * by the application without having to resort to decoding.
   */
  elf = sysprof_elf_symbolizer_new ();
  sysprof_document_loader_set_symbolizer (loader, elf);

  if (!(document = dex_await_object (dex_async_pair_new (loader, &load_info), &error)))
    return dex_future_new_for_error (g_steal_pointer (&error));
  g_assert (SYSPROF_IS_DOCUMENT (document));

  if (!(bytes = dex_await_boxed (dex_async_pair_new (document, &serialize_info), &error)))
    return dex_future_new_for_error (g_steal_pointer (&error));

  _sysprof_recording_add_file_data (recording,
                                    "__symbols__",
                                    g_bytes_get_data (bytes, NULL),
                                    g_bytes_get_size (bytes),
                                    TRUE);

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
sysprof_symbols_bundle_augment (SysprofInstrument *instrument,
                                SysprofRecording  *recording)
{
  g_assert (SYSPROF_IS_SYMBOLS_BUNDLE (instrument));
  g_assert (SYSPROF_IS_RECORDING (recording));

  return dex_scheduler_spawn (NULL, 0,
                              sysprof_symbols_bundle_augment_fiber,
                              g_object_ref (recording),
                              g_object_unref);
}

static void
sysprof_symbols_bundle_class_init (SysprofSymbolsBundleClass *klass)
{
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  instrument_class->augment = sysprof_symbols_bundle_augment;
}

static void
sysprof_symbols_bundle_init (SysprofSymbolsBundle *self)
{
}

/**
 * sysprof_symbols_bundle_new:
 * @self: a #SysprofSymbolsBundle
 *
 * Creates a new instrment that will decode symbols at the end of a recording
 * and attach them to the recording.
 *
 * Returns: (transfer full): a #SysprofSymbolsBundle
 */
SysprofInstrument *
sysprof_symbols_bundle_new (void)
{
  return g_object_new (SYSPROF_TYPE_SYMBOLS_BUNDLE, NULL);
}
