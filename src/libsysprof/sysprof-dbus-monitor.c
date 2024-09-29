/* sysprof-dbus-monitor.c
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

#include <gio/gio.h>

#include "sysprof-instrument-private.h"
#include "sysprof-dbus-monitor.h"
#include "sysprof-recording-private.h"

#define MAX_EMBEDDABLE_MESSAGE_SIZE ((1<<16)-sizeof(SysprofCaptureDBusMessage)-16)

struct _SysprofDBusMonitor
{
  SysprofInstrument parent_instance;
  GBusType bus_type;
  char *bus_address;
};

struct _SysprofDBusMonitorClass
{
  SysprofInstrumentClass parent_instance;
};

enum {
  PROP_0,
  PROP_BUS_ADDRESS,
  PROP_BUS_TYPE,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofDBusMonitor, sysprof_dbus_monitor, SYSPROF_TYPE_INSTRUMENT)

static GParamSpec *properties [N_PROPS];

static char **
sysprof_dbus_monitor_list_required_policy (SysprofInstrument *instrument)
{
  /* TODO: If we add a new policy for monitoring the system bus, then we can
   *       make sysprofd open the monitor and feed that data to the instrument
   *       for recording. That would allow for monitoring --system without
   *       having to be in fallback observation mode.
   *
   *       "org.gnome.sysprof3.profile" is probably enough though.
   */
  return NULL;
}

typedef struct _Record
{
  SysprofRecording *recording;
  DexFuture        *cancellable;
  char             *bus_address;
  GBusType          bus_type;
} Record;

static void
record_free (gpointer data)
{
  Record *record = data;

  g_clear_object (&record->recording);
  dex_clear (&record->cancellable);
  g_free (record);
}

static DexFuture *
sysprof_dbus_monitor_record_fiber (gpointer user_data)
{
  Record *record = user_data;
  g_autofree guint8 *read_buffer = NULL;
  g_autoptr(GSubprocessLauncher) launcher = NULL;
  g_autoptr(GSubprocess) subprocess = NULL;
  g_autoptr(GPtrArray) argv = NULL;
  g_autoptr(GError) error = NULL;
  SysprofCaptureWriter *writer;
  GInputStream *stdout_stream;

  g_assert (record != NULL);
  g_assert (SYSPROF_IS_RECORDING (record->recording));
  g_assert (DEX_IS_FUTURE (record->cancellable));

  writer = _sysprof_recording_writer (record->recording);
  read_buffer = g_malloc (MAX_EMBEDDABLE_MESSAGE_SIZE);
  argv = g_ptr_array_new_null_terminated (0, NULL, TRUE);

  g_ptr_array_add (argv, (char *)"dbus-monitor");
  g_ptr_array_add (argv, (char *)"--profile");
  g_ptr_array_add (argv, (char *)"--binary");

  if (record->bus_address != NULL)
    {
      g_ptr_array_add (argv, (char *)"--address");
      g_ptr_array_add (argv, (char *)record->bus_address);
    }
  else if (record->bus_type == G_BUS_TYPE_SESSION)
    {
      g_ptr_array_add (argv, (char *)"--session");
    }
  else if (record->bus_type == G_BUS_TYPE_SYSTEM)
    {
      g_ptr_array_add (argv, (char *)"--system");
    }
  else
    {
      _sysprof_recording_diagnostic (record->recording,
                                     "D-Bus Monitor",
                                     "Misconfigured D-Bus monitor, will not record");
      return dex_future_new_for_boolean (TRUE);
    }

  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE|G_SUBPROCESS_FLAGS_STDERR_SILENCE);
  subprocess = g_subprocess_launcher_spawnv (launcher, (const char * const *)argv->pdata, &error);

  if (subprocess == NULL)
    return dex_future_new_for_error (g_steal_pointer (&error));

  stdout_stream = g_subprocess_get_stdout_pipe (subprocess);

  for (;;)
    {
      g_autoptr(DexFuture) header_future = NULL;
      guint8 header[16];
      gssize n_needed;

      /* Read enough bytes to decode message length */
      header_future = dex_input_stream_read (stdout_stream,
                                             header,
                                             sizeof header,
                                             G_PRIORITY_DEFAULT);

      /* Wait until we have incoming data or the recording is cancelled */
      if (!dex_await (dex_future_first (dex_ref (record->cancellable),
                                        dex_ref (header_future),
                                        NULL), &error))
        break;

      /* Stop if we reached EOF or errored on reading message header */
      if (dex_await_int64 (dex_ref (header_future), &error) != sizeof header)
        break;

      /* If we got a decoding error, just bail */
      if ((n_needed = g_dbus_message_bytes_needed (header, sizeof header, NULL)) < 0)
        break;

      g_assert (n_needed >= 16);

      /* If this is a message that will be too large to store, then note that
       * and skip the appropriate number of bytes. Our largest frame size is
       * 64k - sizeof(SysprofCaptureFrameDBus).
       */
      if (n_needed > MAX_EMBEDDABLE_MESSAGE_SIZE)
        {
          sysprof_capture_writer_add_dbus_message (writer,
                                                   SYSPROF_CAPTURE_CURRENT_TIME,
                                                   -1,
                                                   -1,
                                                   record->bus_type,
                                                   SYSPROF_CAPTURE_DBUS_FLAGS_MESSAGE_TOO_LARGE,
                                                   NULL, 0);

          /* Skip the remaining bytes for the message or bail on cancellation */
          if (!dex_await (dex_future_first (dex_ref (record->cancellable),
                                            dex_input_stream_skip (stdout_stream,
                                                                   n_needed - sizeof header,
                                                                   G_PRIORITY_DEFAULT),
                                            NULL), &error))
            break;
        }
      else
        {
          g_autoptr(DexFuture) body_future = NULL;

          /* Read the rest of the incoming message */
          memcpy (read_buffer, header, sizeof header);
          body_future = dex_input_stream_read (stdout_stream,
                                               read_buffer + sizeof header,
                                               n_needed - sizeof header,
                                               G_PRIORITY_DEFAULT);

          /* Wait for body or cancellation of recording */
          if (!dex_await (dex_future_first (dex_ref (record->cancellable),
                                            dex_ref (body_future),
                                            NULL), &error))
            break;

          /* If we didn't read what we expected to, just bail */
          if (dex_await_int64 (dex_ref (body_future), &error) != n_needed - sizeof header)
            break;

          sysprof_capture_writer_add_dbus_message (writer,
                                                   SYSPROF_CAPTURE_CURRENT_TIME,
                                                   -1,
                                                   -1,
                                                   record->bus_type,
                                                   0,
                                                   read_buffer, n_needed);
        }
    }

  g_subprocess_force_exit (subprocess);

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
sysprof_dbus_monitor_record (SysprofInstrument *instrument,
                             SysprofRecording  *recording,
                             GCancellable      *cancellable)
{
  SysprofDBusMonitor *self = (SysprofDBusMonitor *)instrument;
  Record *record;

  g_assert (SYSPROF_IS_DBUS_MONITOR (self));
  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (G_IS_CANCELLABLE (cancellable));

  record = g_new0 (Record, 1);
  record->recording = g_object_ref (recording);
  record->cancellable = dex_cancellable_new_from_cancellable (cancellable);
  record->bus_type = self->bus_type;
  record->bus_address = g_strdup (self->bus_address);

  return dex_scheduler_spawn (NULL, 0,
                              sysprof_dbus_monitor_record_fiber,
                              record,
                              record_free);
}

static void
sysprof_dbus_monitor_finalize (GObject *object)
{
  SysprofDBusMonitor *self = (SysprofDBusMonitor *)object;

  g_clear_pointer (&self->bus_address, g_free);

  G_OBJECT_CLASS (sysprof_dbus_monitor_parent_class)->finalize (object);
}

static void
sysprof_dbus_monitor_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  SysprofDBusMonitor *self = SYSPROF_DBUS_MONITOR (object);

  switch (prop_id)
    {
    case PROP_BUS_ADDRESS:
      g_value_set_string (value, sysprof_dbus_monitor_get_bus_address (self));
      break;

    case PROP_BUS_TYPE:
      g_value_set_enum (value, sysprof_dbus_monitor_get_bus_type (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_dbus_monitor_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  SysprofDBusMonitor *self = SYSPROF_DBUS_MONITOR (object);

  switch (prop_id)
    {
    case PROP_BUS_ADDRESS:
      self->bus_address = g_value_dup_string (value);
      break;

    case PROP_BUS_TYPE:
      self->bus_type = g_value_get_enum (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_dbus_monitor_class_init (SysprofDBusMonitorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  object_class->finalize = sysprof_dbus_monitor_finalize;
  object_class->get_property = sysprof_dbus_monitor_get_property;
  object_class->set_property = sysprof_dbus_monitor_set_property;

  instrument_class->list_required_policy = sysprof_dbus_monitor_list_required_policy;
  instrument_class->record = sysprof_dbus_monitor_record;

  properties[PROP_BUS_ADDRESS] =
    g_param_spec_string ("bus-address", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties[PROP_BUS_TYPE] =
    g_param_spec_enum ("bus-type", NULL, NULL,
                       G_TYPE_BUS_TYPE,
                       G_BUS_TYPE_SESSION,
                       (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_dbus_monitor_init (SysprofDBusMonitor *self)
{
}

SysprofInstrument *
sysprof_dbus_monitor_new (GBusType bus_type)
{
  g_return_val_if_fail (bus_type == G_BUS_TYPE_SESSION || bus_type == G_BUS_TYPE_SYSTEM,
                        NULL);

  return g_object_new (SYSPROF_TYPE_DBUS_MONITOR,
                       "bus-type", bus_type,
                       NULL);
}

SysprofInstrument *
sysprof_dbus_monitor_new_for_bus_address (const char *bus_address)
{
  g_return_val_if_fail (bus_address != NULL, NULL);

  return g_object_new (SYSPROF_TYPE_DBUS_MONITOR,
                       "bus-address", bus_address,
                       NULL);
}

GBusType
sysprof_dbus_monitor_get_bus_type (SysprofDBusMonitor *self)
{
  g_return_val_if_fail (SYSPROF_IS_DBUS_MONITOR (self), 0);

  return self->bus_type;
}

const char *
sysprof_dbus_monitor_get_bus_address (SysprofDBusMonitor *self)
{
  g_return_val_if_fail (SYSPROF_IS_DBUS_MONITOR (self), NULL);

  return self->bus_address;
}
