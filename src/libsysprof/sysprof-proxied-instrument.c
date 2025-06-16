/* sysprof-proxied-instrument.c
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

#include <glib/gstdio.h>

#include "sysprof-proxied-instrument-private.h"
#include "sysprof-recording-private.h"

enum {
  PROP_0,
  PROP_BUS_TYPE,
  PROP_BUS_NAME,
  PROP_OBJECT_PATH,
  N_PROPS
};

G_DEFINE_TYPE (SysprofProxiedInstrument, sysprof_proxied_instrument, SYSPROF_TYPE_INSTRUMENT)

static GParamSpec *properties [N_PROPS];

typedef struct _Record
{
  SysprofRecording *recording;
  DexFuture *cancellable;
  char *bus_name;
  char *object_path;
  GVariant *options;
  GBusType bus_type;
  guint call_stop_first : 1;
} Record;

static void
record_free (Record *record)
{
  g_clear_object (&record->recording);
  dex_clear (&record->cancellable);
  g_clear_pointer (&record->bus_name, g_free);
  g_clear_pointer (&record->object_path, g_free);
  g_clear_pointer (&record->options, g_variant_unref);
  g_free (record);
}

static DexFuture *
sysprof_proxied_instrument_record_fiber (gpointer user_data)
{
  Record *record = user_data;
  SysprofCaptureWriter *writer;
  SysprofCaptureReader *reader;
  g_autoptr(GDBusConnection) connection = NULL;
  g_autoptr(GUnixFDList) fd_list = NULL;
  g_autoptr(DexFuture) started = NULL;
  g_autoptr(GError) error = NULL;
  g_autofd int proxy_fd = -1;
  int handle;

  g_assert (record != NULL);
  g_assert (record->bus_type != G_BUS_TYPE_NONE);
  g_assert (record->bus_name != NULL);
  g_assert (record->object_path != NULL);
  g_assert (SYSPROF_IS_RECORDING (record->recording));
  g_assert (DEX_IS_CANCELLABLE (record->cancellable));

  /* Wait for our connection to be available */
  if (!(connection = dex_await_object (dex_bus_get (record->bus_type), &error)))
    return dex_future_new_for_error (g_steal_pointer (&error));

  /* If we should try calling stop first to cancel any in-flight
   * recording. We could pipeline this, but if the other side
   * handles messages on threads, they would race.
   */
  if (record->call_stop_first)
    dex_await (dex_dbus_connection_call (connection,
                                         record->bus_name,
                                         record->object_path,
                                         "org.gnome.Sysprof3.Profiler",
                                         "Stop",
                                         g_variant_new ("()"),
                                         G_VARIANT_TYPE ("()"),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         -1),
               NULL);

  /* Create a new FD that the peer will be able to write to and
   * we will concatenate into the capture after recording.
   */
  if (-1 == (proxy_fd = sysprof_memfd_create ("[sysprof-proxy]")))
    return dex_future_new_for_errno (errno);

  /* Create FDList to pass to the peer so they can get our FD */
  fd_list = g_unix_fd_list_new ();
  if (-1 == (handle = g_unix_fd_list_append (fd_list, proxy_fd, &error)))
    return dex_future_new_for_error (g_steal_pointer (&error));

  /* Call the proxy and give it our FD to start recording */
  if (!dex_await (dex_dbus_connection_call_with_unix_fd_list (connection,
                                                              record->bus_name,
                                                              record->object_path,
                                                              "org.gnome.Sysprof3.Profiler",
                                                              "Start",
                                                              g_variant_new ("(@a{sv}h)",
                                                                             record->options,
                                                                             handle),
                                                              G_VARIANT_TYPE ("()"),
                                                              G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION,
                                                              G_MAXUINT,
                                                              fd_list),
                  &error))
    {
      g_debug ("Failed to start profiler at %s %s: %s",
               record->bus_name,
               record->object_path,
               error->message);
      return dex_future_new_for_error (g_steal_pointer (&error));
    }

  /* Await completion of recording */
  dex_await (dex_ref (record->cancellable), NULL);

  /* Call the proxy and let them know to stop */
  dex_await (dex_dbus_connection_call (connection,
                                       record->bus_name,
                                       record->object_path,
                                       "org.gnome.Sysprof3.Profiler",
                                       "Stop",
                                       g_variant_new ("()"),
                                       G_VARIANT_TYPE ("()"),
                                       G_DBUS_CALL_FLAGS_NONE,
                                       -1),
             &error);

  if (error != NULL)
    g_warning ("Failed to stop profiler at %s %s: %s",
               record->bus_name,
               record->object_path,
               error->message);

  /* Reset the file position to the start */
  lseek (proxy_fd, 0, SEEK_SET);

  /* Now cat the recording into our writer */
  writer = _sysprof_recording_writer (record->recording);

  if ((reader = sysprof_capture_reader_new_from_fd (g_steal_fd (&proxy_fd))))
    {
      sysprof_capture_writer_cat (writer, reader);
      sysprof_capture_reader_unref (reader);
    }

  return dex_future_new_for_boolean (TRUE);
}

static GVariant *
create_empty_dictionary (void)
{
  GVariantDict dict = G_VARIANT_DICT_INIT (NULL);
  return g_variant_take_ref (g_variant_dict_end (&dict));
}

static DexFuture *
sysprof_proxied_instrument_record (SysprofInstrument *instrument,
                                   SysprofRecording  *recording,
                                   GCancellable      *cancellable)
{
  SysprofProxiedInstrument *self = (SysprofProxiedInstrument *)instrument;
  Record *record;

  g_assert (SYSPROF_IS_PROXIED_INSTRUMENT (self));
  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (G_IS_CANCELLABLE (cancellable));

  record = g_new0 (Record, 1);
  record->recording = g_object_ref (recording);
  record->cancellable = dex_cancellable_new_from_cancellable (cancellable);
  record->bus_name = g_strdup (self->bus_name);
  record->object_path = g_strdup (self->object_path);
  record->bus_type = self->bus_type;
  record->call_stop_first = self->call_stop_first;

  if (self->options != NULL)
    record->options = g_variant_ref (self->options);
  else
    record->options = create_empty_dictionary ();

  return dex_scheduler_spawn (NULL, 0,
                              sysprof_proxied_instrument_record_fiber,
                              record,
                              (GDestroyNotify)record_free);
}

static void
sysprof_proxied_instrument_finalize (GObject *object)
{
  SysprofProxiedInstrument *self = (SysprofProxiedInstrument *)object;

  g_clear_pointer (&self->bus_name, g_free);
  g_clear_pointer (&self->object_path, g_free);
  g_clear_pointer (&self->options, g_variant_unref);

  G_OBJECT_CLASS (sysprof_proxied_instrument_parent_class)->finalize (object);
}

static void
sysprof_proxied_instrument_get_property (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
  SysprofProxiedInstrument *self = SYSPROF_PROXIED_INSTRUMENT (object);

  switch (prop_id)
    {
    case PROP_BUS_TYPE:
      g_value_set_enum (value, self->bus_type);
      break;

    case PROP_BUS_NAME:
      g_value_set_string (value, self->bus_name);
      break;

    case PROP_OBJECT_PATH:
      g_value_set_string (value, self->object_path);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_proxied_instrument_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
  SysprofProxiedInstrument *self = SYSPROF_PROXIED_INSTRUMENT (object);

  switch (prop_id)
    {
    case PROP_BUS_TYPE:
      self->bus_type = g_value_get_enum (value);
      break;

    case PROP_BUS_NAME:
      self->bus_name = g_value_dup_string (value);
      break;

    case PROP_OBJECT_PATH:
      self->object_path = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_proxied_instrument_class_init (SysprofProxiedInstrumentClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  object_class->finalize = sysprof_proxied_instrument_finalize;
  object_class->get_property = sysprof_proxied_instrument_get_property;
  object_class->set_property = sysprof_proxied_instrument_set_property;

  instrument_class->record = sysprof_proxied_instrument_record;

  properties [PROP_BUS_TYPE] =
    g_param_spec_enum ("bus-type", NULL, NULL,
                       G_TYPE_BUS_TYPE,
                       G_BUS_TYPE_NONE,
                       (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_BUS_NAME] =
    g_param_spec_string ("bus-name", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_OBJECT_PATH] =
    g_param_spec_string ("object-path", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_proxied_instrument_init (SysprofProxiedInstrument *self)
{
}

SysprofInstrument *
sysprof_proxied_instrument_new_with_options (GBusType    bus_type,
                                             const char *bus_name,
                                             const char *object_path,
                                             GVariant   *options)
{
  SysprofProxiedInstrument *self;

  g_return_val_if_fail (bus_type == G_BUS_TYPE_SYSTEM ||
                        bus_type == G_BUS_TYPE_SESSION, NULL);
  g_return_val_if_fail (bus_name != NULL, NULL);
  g_return_val_if_fail (object_path != NULL, NULL);
  g_return_val_if_fail (options == NULL || g_variant_is_of_type (options, G_VARIANT_TYPE_VARDICT), NULL);

  self = g_object_new (SYSPROF_TYPE_PROXIED_INSTRUMENT,
                       "bus-type", bus_type,
                       "bus-name", bus_name,
                       "object-path", object_path,
                       NULL);

  if (options != NULL)
    self->options = g_variant_ref_sink (options);

  return SYSPROF_INSTRUMENT (self);
}

SysprofInstrument *
sysprof_proxied_instrument_new (GBusType    bus_type,
                                const char *bus_name,
                                const char *object_path)
{
  return sysprof_proxied_instrument_new_with_options (bus_type, bus_name, object_path, NULL);
}
