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

#include "sysprof-document-loader-private.h"
#include "sysprof-document-private.h"

#include "sysprof-debuginfod-symbolizer.h"
#include "sysprof-instrument-private.h"
#include "sysprof-recording-private.h"
#include "sysprof-symbols-bundle.h"

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureReader, sysprof_capture_reader_unref)

struct _SysprofSymbolsBundle
{
  SysprofInstrument parent_instance;
  guint enable_debuginfod : 1;
};

struct _SysprofSymbolsBundleClass
{
  SysprofInstrumentClass parent_class;
};

G_DEFINE_FINAL_TYPE (SysprofSymbolsBundle, sysprof_symbols_bundle, SYSPROF_TYPE_INSTRUMENT)

enum {
  PROP_0,
  PROP_ENABLE_DEBUGINFOD,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

typedef struct _Augment
{
  SysprofRecording *recording;
  guint enable_debuginfod : 1;
} Augment;

static void
augment_free (Augment *augment)
{
  g_clear_object (&augment->recording);
  g_free (augment);
}

static DexFuture *
sysprof_symbols_bundle_augment_fiber (gpointer user_data)
{
  Augment *augment = user_data;
  SysprofRecording *recording = augment->recording;
  g_autoptr(SysprofDocumentLoader) loader = NULL;
  g_autoptr(SysprofMultiSymbolizer) multi = NULL;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(GBytes) bytes = NULL;
  g_autoptr(GError) error = NULL;
  g_autofd int fd = -1;

  g_assert (SYSPROF_IS_RECORDING (recording));

  if (-1 == (fd = sysprof_recording_dup_fd (recording)))
    return dex_future_new_for_errno (errno);
  g_assert (fd > -1);

  if (!(loader = sysprof_document_loader_new_for_fd (fd, &error)))
    return dex_future_new_for_error (g_steal_pointer (&error));
  g_assert (SYSPROF_IS_DOCUMENT_LOADER (loader));

  multi = sysprof_multi_symbolizer_new ();
  sysprof_multi_symbolizer_take (multi, sysprof_elf_symbolizer_new ());

  if (augment->enable_debuginfod)
    {
      g_autoptr(SysprofSymbolizer) debuginfod = NULL;
      g_autoptr(GError) debuginfod_error = NULL;

      if (!(debuginfod = sysprof_debuginfod_symbolizer_new (&debuginfod_error)))
        g_warning ("Failed to create debuginfod symbolizer: %s", debuginfod_error->message);
      else
        sysprof_multi_symbolizer_take (multi, g_steal_pointer (&debuginfod));
    }

  sysprof_document_loader_set_symbolizer (loader, SYSPROF_SYMBOLIZER (multi));

  if (!(document = dex_await_object (_sysprof_document_loader_load (loader), &error)))
    return dex_future_new_for_error (g_steal_pointer (&error));
  g_assert (SYSPROF_IS_DOCUMENT (document));

  if (!(bytes = dex_await_boxed (_sysprof_document_serialize_symbols (document), &error)))
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
  SysprofSymbolsBundle *self = (SysprofSymbolsBundle *)instrument;
  Augment *state;

  g_assert (SYSPROF_IS_SYMBOLS_BUNDLE (self));
  g_assert (SYSPROF_IS_RECORDING (recording));

  state = g_new0 (Augment, 1);
  state->recording = g_object_ref (recording);
  state->enable_debuginfod = self->enable_debuginfod;

  return dex_scheduler_spawn (NULL, 0,
                              sysprof_symbols_bundle_augment_fiber,
                              state,
                              (GDestroyNotify) augment_free);
}

static void
sysprof_symbols_bundle_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  SysprofSymbolsBundle *self = SYSPROF_SYMBOLS_BUNDLE (object);

  switch (prop_id)
    {
    case PROP_ENABLE_DEBUGINFOD:
      g_value_set_boolean (value, sysprof_symbols_bundle_get_enable_debuginfod (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_symbols_bundle_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  SysprofSymbolsBundle *self = SYSPROF_SYMBOLS_BUNDLE (object);

  switch (prop_id)
    {
    case PROP_ENABLE_DEBUGINFOD:
      sysprof_symbols_bundle_set_enable_debuginfod (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_symbols_bundle_class_init (SysprofSymbolsBundleClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  object_class->get_property = sysprof_symbols_bundle_get_property;
  object_class->set_property = sysprof_symbols_bundle_set_property;

  instrument_class->augment = sysprof_symbols_bundle_augment;

  properties[PROP_ENABLE_DEBUGINFOD] =
    g_param_spec_boolean ("enable-debuginfod", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

}

static void
sysprof_symbols_bundle_init (SysprofSymbolsBundle *self)
{
}

/**
 * sysprof_symbols_bundle_new:
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

gboolean
sysprof_symbols_bundle_get_enable_debuginfod (SysprofSymbolsBundle *self)
{
  g_return_val_if_fail (SYSPROF_IS_SYMBOLS_BUNDLE (self), FALSE);

  return self->enable_debuginfod;
}

void
sysprof_symbols_bundle_set_enable_debuginfod (SysprofSymbolsBundle *self,
                                              gboolean              enable_debuginfod)
{
  g_return_if_fail (SYSPROF_IS_SYMBOLS_BUNDLE (self));

  enable_debuginfod = !!enable_debuginfod;

  if (enable_debuginfod != self->enable_debuginfod)
    {
      self->enable_debuginfod = enable_debuginfod;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLE_DEBUGINFOD]);
    }
}
