/* sysprof-screencast.c
 *
 * Copyright 2026 Christian Hergert
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

#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <stdlib.h>

#include "sysprof-instrument-private.h"
#include "sysprof-recording-private.h"
#include "sysprof-screencast.h"

#define SCREENCAST_BUS "org.gnome.Mutter.ScreenCast"
#define SCREENCAST_PATH "/org/gnome/Mutter/ScreenCast"
#define SCREENCAST_FILE "/screencast.webm"
#define MAX_CHUNK_SIZE 60000
#define MAX_QUEUED_BYTES (8 * 1024 * 1024)

struct _SysprofScreencast
{
  SysprofInstrument parent_instance;
  int               x;
  int               y;
  int               width;
  int               height;
};

struct _SysprofScreencastClass
{
  SysprofInstrumentClass parent_class;
};

typedef struct
{
  SysprofRecording *recording;
  int               x;
  int               y;
  int               width;
  int               height;
  GstElement       *pipeline;
  DexPromise       *node_ready;
  DexPromise       *finished;
  GCancellable     *cancellable;
  GDBusConnection  *connection;
  char             *session_path;
  char             *stream_path;
  guint             stream_subscription;
  guint             bus_watch;
  GMutex            queue_mutex;
  GCond             queue_cond;
  gsize             queued_bytes;
  gboolean          anchored;
} Record;

typedef struct
{
  Record           *owner;
  SysprofRecording *recording;
  GBytes           *bytes;
} Chunk;

typedef struct
{
  SysprofRecording *recording;
  gint64            time;
} Anchor;

G_DEFINE_FINAL_TYPE (SysprofScreencast, sysprof_screencast, SYSPROF_TYPE_INSTRUMENT)

static void
chunk_free (Chunk *chunk)
{
  g_clear_object (&chunk->recording);
  g_clear_pointer (&chunk->bytes, g_bytes_unref);
  g_free (chunk);
}

static gboolean
write_chunk (gpointer user_data)
{
  Chunk *chunk = user_data;
  Record *record = chunk->owner;
  SysprofCaptureWriter *writer = _sysprof_recording_writer (chunk->recording);
  gsize size;
  const guint8 *data = g_bytes_get_data (chunk->bytes, &size);

  for (gsize offset = 0; offset < size; offset += MAX_CHUNK_SIZE)
    sysprof_capture_writer_add_file (writer,
                                     SYSPROF_CAPTURE_CURRENT_TIME,
                                     -1,
                                     -1,
                                     SCREENCAST_FILE,
                                     FALSE,
                                     data + offset,
                                     MIN (size - offset, MAX_CHUNK_SIZE));

  g_mutex_lock (&record->queue_mutex);
  record->queued_bytes -= size;
  g_cond_signal (&record->queue_cond);
  g_mutex_unlock (&record->queue_mutex);

  chunk_free (chunk);

  return G_SOURCE_REMOVE;
}

static gboolean
write_anchor (gpointer user_data)
{
  Anchor *anchor = user_data;
  g_autofree char *value = g_strdup_printf ("%" G_GINT64_FORMAT, anchor->time);

  sysprof_capture_writer_add_metadata (_sysprof_recording_writer (anchor->recording),
                                       anchor->time,
                                       -1,
                                       -1,
                                       "sysprof.screencast.start-nsec",
                                       value,
                                       -1);

  g_object_unref (anchor->recording);
  g_free (anchor);

  return G_SOURCE_REMOVE;
}

static GstFlowReturn
new_sample (GstAppSink *sink,
            gpointer    user_data)
{
  Record *record = user_data;
  GstSample *sample = gst_app_sink_pull_sample (sink);
  GstBuffer *buffer;
  GstMapInfo map;
  Chunk *chunk;

  if (sample == NULL)
    return GST_FLOW_EOS;

  buffer = gst_sample_get_buffer (sample);

  if (buffer == NULL || !gst_buffer_map (buffer, &map, GST_MAP_READ))
    {
      gst_sample_unref (sample);

      return GST_FLOW_ERROR;
    }

  chunk = g_new0 (Chunk, 1);
  chunk->owner = record;
  chunk->recording = g_object_ref (record->recording);
  chunk->bytes = g_bytes_new (map.data, map.size);

  g_mutex_lock (&record->queue_mutex);

  while (record->queued_bytes >= MAX_QUEUED_BYTES)
    g_cond_wait (&record->queue_cond, &record->queue_mutex);

  record->queued_bytes += map.size;
  g_mutex_unlock (&record->queue_mutex);

  gst_buffer_unmap (buffer, &map);
  gst_sample_unref (sample);

  g_main_context_invoke (NULL, write_chunk, chunk);

  return GST_FLOW_OK;
}

static GstPadProbeReturn
first_frame (GstPad          *pad,
             GstPadProbeInfo *info,
             gpointer         user_data)
{
  Record *record = user_data;
  GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER (info);
  GstClock *clock;
  gint64 before;
  gint64 after;
  GstClockTime now;
  gint64 start;
  gint64 absolute_start;
  gint64 running_start;
  gint64 midpoint;
  Anchor *anchor;

  if (record->anchored ||
      buffer == NULL ||
      !GST_BUFFER_PTS_IS_VALID (buffer))
    return GST_PAD_PROBE_OK;

  clock = gst_element_get_clock (record->pipeline);

  if (clock == NULL)
    return GST_PAD_PROBE_OK;

  before = SYSPROF_CAPTURE_CURRENT_TIME;
  now = gst_clock_get_time (clock);
  after = SYSPROF_CAPTURE_CURRENT_TIME;
  gst_object_unref (clock);

  midpoint = before + (after - before) / 2;
  absolute_start = midpoint + (gint64)GST_BUFFER_PTS (buffer) - (gint64)now;
  running_start = midpoint + (gint64)GST_BUFFER_PTS (buffer) -
                  (gint64)(now - gst_element_get_base_time (record->pipeline));
  start = llabs (absolute_start - midpoint) < llabs (running_start - midpoint)
        ? absolute_start : running_start;

  anchor = g_new0 (Anchor, 1);
  anchor->recording = g_object_ref (record->recording);
  anchor->time = start;
  record->anchored = TRUE;

  g_main_context_invoke (NULL, write_anchor, anchor);

  return GST_PAD_PROBE_REMOVE;
}

static gboolean
bus_message (GstBus     *bus,
             GstMessage *message,
             gpointer    user_data)
{
  Record *record = user_data;

  if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR)
    {
      g_autoptr(GError) error = NULL;

      gst_message_parse_error (message, &error, NULL);
      _sysprof_recording_diagnostic (record->recording, "Screencast", "%s", error->message);
      dex_promise_resolve_boolean (record->finished, FALSE);
      record->bus_watch = 0;

      return G_SOURCE_REMOVE;
    }

  if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_EOS)
    {
      dex_promise_resolve_boolean (record->finished, TRUE);
      record->bus_watch = 0;

      return G_SOURCE_REMOVE;
    }

  return G_SOURCE_CONTINUE;
}

static void
stream_signal (GDBusConnection *connection,
               const char      *sender,
               const char      *path,
               const char      *interface,
               const char      *signal,
               GVariant        *parameters,
               gpointer         user_data)
{
  Record *record = user_data;
  guint node_id;

  g_variant_get (parameters, "(u)", &node_id);
  dex_promise_resolve_uint (record->node_ready, node_id);
}

static void
record_free (gpointer user_data)
{
  Record *record = user_data;

  if (record->stream_subscription)
    g_dbus_connection_signal_unsubscribe (record->connection, record->stream_subscription);

  if (record->bus_watch)
    g_source_remove (record->bus_watch);

  if (record->pipeline)
    {
      gst_element_set_state (record->pipeline, GST_STATE_NULL);
      gst_object_unref (record->pipeline);
    }

  g_clear_object (&record->recording);
  g_clear_object (&record->cancellable);
  g_clear_object (&record->connection);
  dex_clear (&record->node_ready);
  dex_clear (&record->finished);
  g_free (record->session_path);
  g_free (record->stream_path);
  g_mutex_clear (&record->queue_mutex);
  g_cond_clear (&record->queue_cond);
  g_free (record);
}

static GVariant *
record_properties (void)
{
  GVariantBuilder builder;

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&builder, "{sv}", "is-recording", g_variant_new_boolean (TRUE));
  g_variant_builder_add (&builder, "{sv}", "cursor-mode", g_variant_new_uint32 (1));

  return g_variant_builder_end (&builder);
}

static void
drain_chunks (Record *record)
{
  while (TRUE)
    {
      gsize queued;

      g_mutex_lock (&record->queue_mutex);
      queued = record->queued_bytes;
      g_mutex_unlock (&record->queue_mutex);

      if (queued == 0)
        return;

      dex_await (dex_timeout_new_usec (1000), NULL);
    }
}

static DexFuture *
record_fiber (gpointer user_data)
{
  Record *record = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) reply = NULL;
  g_autoptr(GstBus) bus = NULL;
  g_autoptr(GstElement) source = NULL;
  g_autoptr(GstElement) sink = NULL;
  g_autoptr(GstElement) encoder = NULL;
  g_autoptr(GstElementFactory) source_factory = NULL;
  g_autoptr(GstClock) system_clock = NULL;
  g_autoptr(GstPad) pad = NULL;
  g_autoptr(DexFuture) cancelled = NULL;
  guint node_id;
  g_autofree char *node_path = NULL;
  gboolean completed = FALSE;

  if (!(record->connection = dex_await_object (dex_bus_get (G_BUS_TYPE_SESSION), &error)))
    goto failure;

  reply = dex_await_variant (dex_dbus_connection_call (record->connection,
                                                       SCREENCAST_BUS,
                                                       SCREENCAST_PATH,
                                                       SCREENCAST_BUS,
                                                       "CreateSession",
                                                       g_variant_new ("(@a{sv})",
                                                                      g_variant_new_array (G_VARIANT_TYPE ("{sv}"),
                                                                                           NULL,
                                                                                           0)),
                                                       G_VARIANT_TYPE ("(o)"),
                                                       G_DBUS_CALL_FLAGS_NONE,
                                                       -1),
                             &error);

  if (reply == NULL)
    goto failure;

  g_variant_get (reply, "(o)", &record->session_path);
  g_clear_pointer (&reply, g_variant_unref);

  if (record->width > 0 && record->height > 0)
    reply = dex_await_variant (dex_dbus_connection_call (record->connection,
                                                         SCREENCAST_BUS,
                                                         record->session_path,
                                                         SCREENCAST_BUS ".Session",
                                                         "RecordArea",
                                                         g_variant_new ("(iiii@a{sv})",
                                                                        record->x,
                                                                        record->y,
                                                                        record->width,
                                                                        record->height,
                                                                        record_properties ()),
                                                         G_VARIANT_TYPE ("(o)"),
                                                         G_DBUS_CALL_FLAGS_NONE,
                                                         -1),
                               &error);
  else
    reply = dex_await_variant (dex_dbus_connection_call (record->connection,
                                                         SCREENCAST_BUS,
                                                         record->session_path,
                                                         SCREENCAST_BUS ".Session",
                                                         "RecordMonitor",
                                                         g_variant_new ("(s@a{sv})",
                                                                        "",
                                                                        record_properties ()),
                                                         G_VARIANT_TYPE ("(o)"),
                                                         G_DBUS_CALL_FLAGS_NONE,
                                                         -1),
                               &error);

  if (reply == NULL)
    goto failure;

  g_variant_get (reply, "(o)", &record->stream_path);
  g_clear_pointer (&reply, g_variant_unref);

  record->node_ready = dex_promise_new ();
  record->stream_subscription =
    g_dbus_connection_signal_subscribe (record->connection,
                                        SCREENCAST_BUS,
                                        SCREENCAST_BUS ".Stream",
                                        "PipeWireStreamAdded",
                                        record->stream_path,
                                        NULL,
                                        G_DBUS_SIGNAL_FLAGS_NONE,
                                        stream_signal,
                                        record,
                                        NULL);

  reply = dex_await_variant (dex_dbus_connection_call (record->connection,
                                                       SCREENCAST_BUS,
                                                       record->session_path,
                                                       SCREENCAST_BUS ".Session",
                                                       "Start",
                                                       NULL,
                                                       G_VARIANT_TYPE ("()"),
                                                       G_DBUS_CALL_FLAGS_NONE,
                                                       -1),
                             &error);

  if (reply == NULL)
    goto failure;

  cancelled = dex_cancellable_new_from_cancellable (record->cancellable);

  if (!dex_await (dex_future_first (dex_ref (DEX_FUTURE (record->node_ready)),
                                    dex_ref (cancelled), NULL),
                  &error))
    goto failure;

  node_id = dex_await_uint (dex_ref (DEX_FUTURE (record->node_ready)), &error);

  if (error != NULL)
    goto failure;

  if (!gst_is_initialized ())
    gst_init (NULL, NULL);

#ifdef PIPEWIRE_GSTREAMER_PLUGIN_DIR
  source_factory = gst_element_factory_find ("pipewiresrc");

  if (source_factory == NULL)
    gst_registry_scan_path (gst_registry_get (), PIPEWIRE_GSTREAMER_PLUGIN_DIR);
#endif

  if (source_factory == NULL)
    source_factory = gst_element_factory_find ("pipewiresrc");

  if (source_factory == NULL)
    {
      g_set_error_literal (&error,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_FOUND,
                           "GStreamer PipeWire source plugin (pipewiresrc) is unavailable");
      goto failure;
    }

  record->pipeline = gst_parse_launch (
    "pipewiresrc name=screen do-timestamp=true keepalive-time=1000 "
    "resend-last=true ! "
    "capsfilter caps=video/x-raw,max-framerate=15/1 ! videoconvertscale ! "
    "video/x-raw,width=[1,1280],height=[1,720],pixel-aspect-ratio=1/1 ! "
    "vp8enc name=encoder deadline=1 keyframe-max-dist=30 ! "
    "webmmux name=mux offset-to-zero=true streamable=true ! "
    "appsink name=output emit-signals=true sync=false",
                                       &error);

  if (record->pipeline == NULL)
    goto failure;

  system_clock = gst_system_clock_obtain ();
  gst_pipeline_use_clock (GST_PIPELINE (record->pipeline), system_clock);

  source = gst_bin_get_by_name (GST_BIN (record->pipeline), "screen");
  sink = gst_bin_get_by_name (GST_BIN (record->pipeline), "output");
  encoder = gst_bin_get_by_name (GST_BIN (record->pipeline), "encoder");

  if (source == NULL ||
      sink == NULL ||
      encoder == NULL)
    goto failure;

  node_path = g_strdup_printf ("%u", node_id);
  g_object_set (source,
                "path", node_path,
                NULL);
  g_object_set (sink,
                "max-buffers", 8,
                NULL);
  g_signal_connect (sink,
                    "new-sample",
                    G_CALLBACK (new_sample),
                    record);

  pad = gst_element_get_static_pad (encoder, "src");
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER, first_frame, record, NULL);

  bus = gst_element_get_bus (record->pipeline);
  record->finished = dex_promise_new ();
  record->bus_watch = gst_bus_add_watch (bus, bus_message, record);

  if (gst_element_set_state (record->pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
    goto failure;

  dex_await (dex_future_first (dex_ref (cancelled),
                               dex_ref (DEX_FUTURE (record->finished)), NULL),
             NULL);

  gst_element_send_event (record->pipeline, gst_event_new_eos ());
  completed = dex_await_boolean (dex_future_first (dex_ref (DEX_FUTURE (record->finished)),
                                                   dex_timeout_new_seconds (5), NULL),
                                 NULL);

  gst_element_set_state (record->pipeline, GST_STATE_NULL);
  drain_chunks (record);

  if (record->anchored && completed)
    {
      dex_await (dex_ref (DEX_FUTURE (record->finished)), NULL);
      sysprof_capture_writer_add_file (_sysprof_recording_writer (record->recording),
                                       SYSPROF_CAPTURE_CURRENT_TIME,
                                       -1,
                                       -1,
                                       SCREENCAST_FILE,
                                       TRUE,
                                       NULL,
                                       0);
    }

  dex_await (dex_dbus_connection_call (record->connection,
                                       SCREENCAST_BUS,
                                       record->session_path,
                                       SCREENCAST_BUS ".Session",
                                       "Stop",
                                       NULL,
                                       G_VARIANT_TYPE ("()"),
                                       G_DBUS_CALL_FLAGS_NONE,
                                       -1),
             NULL);

  return dex_future_new_true ();

failure:
  if (record->pipeline != NULL)
    {
      gst_element_set_state (record->pipeline, GST_STATE_NULL);
      drain_chunks (record);
    }

  _sysprof_recording_diagnostic (record->recording,
                                 "Screencast",
                                 "%s",
                                 error != NULL ? error->message : "Could not start screencast");

  if (record->connection != NULL && record->session_path != NULL)
    dex_await (dex_dbus_connection_call (record->connection,
                                         SCREENCAST_BUS,
                                         record->session_path,
                                         SCREENCAST_BUS ".Session",
                                         "Stop",
                                         NULL,
                                         G_VARIANT_TYPE ("()"),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         -1),
               NULL);

  return dex_future_new_true ();
}

static DexFuture *
sysprof_screencast_record (SysprofInstrument *instrument,
                           SysprofRecording  *recording,
                           GCancellable      *cancellable)
{
  Record *record;

  g_assert (SYSPROF_IS_SCREENCAST (instrument));
  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (G_IS_CANCELLABLE (cancellable));

  record = g_new0 (Record, 1);
  g_mutex_init (&record->queue_mutex);
  g_cond_init (&record->queue_cond);
  record->recording = g_object_ref (recording);
  record->cancellable = g_object_ref (cancellable);
  record->x = SYSPROF_SCREENCAST (instrument)->x;
  record->y = SYSPROF_SCREENCAST (instrument)->y;
  record->width = SYSPROF_SCREENCAST (instrument)->width;
  record->height = SYSPROF_SCREENCAST (instrument)->height;

  return dex_scheduler_spawn (NULL,
                              0,
                              record_fiber,
                              record,
                              record_free);
}

static void
sysprof_screencast_class_init (SysprofScreencastClass *klass)
{
  SYSPROF_INSTRUMENT_CLASS (klass)->record = sysprof_screencast_record;
}

static void
sysprof_screencast_init (SysprofScreencast *self)
{
}

/**
 * sysprof_screencast_new:
 *
 * Creates an instrument which records the primary monitor into
 * `/screencast.webm` in the capture.
 *
 * Returns: (transfer full): a #SysprofInstrument
 */
SysprofInstrument *
sysprof_screencast_new (void)
{
  return g_object_new (SYSPROF_TYPE_SCREENCAST, NULL);
}

/**
 * sysprof_screencast_new_for_area:
 * @x: stage x coordinate
 * @y: stage y coordinate
 * @width: area width
 * @height: area height
 *
 * Records an area containing the monitor layout.
 *
 * Returns: (transfer full): a #SysprofInstrument
 */
SysprofInstrument *
sysprof_screencast_new_for_area (int x,
                                 int y,
                                 int width,
                                 int height)
{
  SysprofScreencast *self;

  g_return_val_if_fail (width > 0, NULL);
  g_return_val_if_fail (height > 0, NULL);

  self = g_object_new (SYSPROF_TYPE_SCREENCAST, NULL);
  self->x = x;
  self->y = y;
  self->width = width;
  self->height = height;

  return SYSPROF_INSTRUMENT (self);
}
