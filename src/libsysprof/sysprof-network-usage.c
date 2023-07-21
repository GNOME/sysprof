/* sysprof-network-usage.c
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

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib-unix.h>
#include <glib/gstdio.h>

#include "line-reader-private.h"

#include "sysprof-network-usage.h"
#include "sysprof-instrument-private.h"
#include "sysprof-recording-private.h"

struct _SysprofNetworkUsage
{
  SysprofInstrument parent_instance;
};

struct _SysprofNetworkUsageClass
{
  SysprofInstrumentClass parent_class;
};

typedef struct _DeviceUsage
{
  /* Counter IDs */
  guint rx_bytes_id;
  guint tx_bytes_id;
  gchar iface[32];
  gint64 rx_bytes;
  gint64 rx_packets;
  gint64 rx_errors;
  gint64 rx_dropped;
  gint64 rx_fifo;
  gint64 rx_frame;
  gint64 rx_compressed;
  gint64 rx_multicast;
  gint64 tx_bytes;
  gint64 tx_packets;
  gint64 tx_errors;
  gint64 tx_dropped;
  gint64 tx_fifo;
  gint64 tx_collisions;
  gint64 tx_carrier;
  gint64 tx_compressed;
} DeviceUsage;

G_DEFINE_FINAL_TYPE (SysprofNetworkUsage, sysprof_network_usage, SYSPROF_TYPE_INSTRUMENT)

typedef struct _Record
{
  SysprofRecording *recording;
  DexFuture        *cancellable;
} Record;

static void
record_free (gpointer data)
{
  Record *record = data;

  g_clear_object (&record->recording);
  dex_clear (&record->cancellable);
  g_free (record);
}

static DeviceUsage *
find_device_by_name (GArray     *ar,
                     const char *name)
{
  for (guint i = 0; i < ar->len; i++)
    {
      DeviceUsage *dev = &g_array_index (ar, DeviceUsage, i);

      if (g_strcmp0 (name, dev->iface) == 0)
        return dev;
    }

  return NULL;
}

static DexFuture *
sysprof_network_usage_record_fiber (gpointer user_data)
{
  g_autoptr(GByteArray) buf = g_byte_array_new ();
  Record *record = user_data;
  g_autofree SysprofCaptureCounterValue *values = NULL;
  g_autofree guint *ids = NULL;
  g_autoptr(GArray) devices = NULL;
  g_autoptr(GError) error = NULL;
  SysprofCaptureWriter *writer;
  SysprofCaptureCounter ctr[2] = {0};
  g_autofd int stat_fd = -1;
  LineReader reader;
  guint combined_rx_id;
  guint combined_tx_id;
  gssize n_read;
  gsize line_len;
  guint lineno;
  char *line;

  g_assert (record != NULL);
  g_assert (SYSPROF_IS_RECORDING (record->recording));
  g_assert (DEX_IS_FUTURE (record->cancellable));

  g_byte_array_set_size (buf, 4096*2);

  writer = _sysprof_recording_writer (record->recording);

  if (-1 == (stat_fd = open ("/proc/net/dev", O_RDONLY|O_CLOEXEC)))
    return dex_future_new_for_errno (errno);

  devices = g_array_new (FALSE, FALSE, sizeof (DeviceUsage));

  combined_rx_id = sysprof_capture_writer_request_counter (writer, 1);
  combined_tx_id = sysprof_capture_writer_request_counter (writer, 1);

  g_strlcpy (ctr[0].category, "Network", sizeof ctr[0].category);
  g_strlcpy (ctr[0].name, "RX Bytes", sizeof ctr[0].name);
  g_strlcpy (ctr[0].description, "Combined", sizeof ctr[0].description);
  ctr[0].id = combined_rx_id;
  ctr[0].type = SYSPROF_CAPTURE_COUNTER_INT64;
  ctr[0].value.v64 = 0;

  g_strlcpy (ctr[1].category, "Network", sizeof ctr[1].category);
  g_strlcpy (ctr[1].name, "TX Bytes", sizeof ctr[1].name);
  g_strlcpy (ctr[1].description, "Combined", sizeof ctr[1].description);
  ctr[1].id = combined_tx_id;
  ctr[1].type = SYSPROF_CAPTURE_COUNTER_INT64;
  ctr[1].value.v64 = 0;

  sysprof_capture_writer_define_counters (writer,
                                          SYSPROF_CAPTURE_CURRENT_TIME,
                                          -1,
                                          -1,
                                          ctr, G_N_ELEMENTS (ctr));

  n_read = dex_await_int64 (dex_aio_read (NULL, stat_fd, buf->data, buf->len, 0), &error);
  if (n_read <= 0)
    return dex_future_new_for_errno (errno);

  lineno = 0;
  line_reader_init (&reader, (char *)buf->data, n_read);
  while ((line = line_reader_next (&reader, &line_len)))
    {
      DeviceUsage dev = {0};
      g_autofree char *rx = NULL;
      g_autofree char *tx = NULL;
      char *ptr = line;
      char *name;

      line[line_len] = 0;

      if (lineno++ < 2)
        continue;

      for (; *ptr && g_ascii_isspace (*ptr); ptr++) { /* Do Nothing */ }
      name = ptr;
      for (; *ptr && *ptr != ':'; ptr++) { /* Do Nothing */ }
      *ptr = 0;

      rx = g_strdup_printf ("RX Bytes (%s)", name);
      tx = g_strdup_printf ("TX Bytes (%s)", name);

      g_strlcpy (ctr[0].category, "Network", sizeof ctr[0].category);
      g_strlcpy (ctr[0].name, rx, sizeof ctr[0].name);
      g_strlcpy (ctr[0].description, name, sizeof ctr[0].description);
      ctr[0].id = sysprof_capture_writer_request_counter (writer, 1);
      ctr[0].type = SYSPROF_CAPTURE_COUNTER_INT64;
      ctr[0].value.v64 = 0;

      g_strlcpy (ctr[1].category, "Network", sizeof ctr[1].category);
      g_strlcpy (ctr[1].name, tx, sizeof ctr[1].name);
      g_strlcpy (ctr[1].description, name, sizeof ctr[1].description);
      ctr[1].id = sysprof_capture_writer_request_counter (writer, 1);
      ctr[1].type = SYSPROF_CAPTURE_COUNTER_INT64;
      ctr[1].value.v64 = 0;

      sysprof_capture_writer_define_counters (writer,
                                              SYSPROF_CAPTURE_CURRENT_TIME,
                                              -1,
                                              -1,
                                              ctr, G_N_ELEMENTS (ctr));

      dev.rx_bytes_id = ctr[0].id;
      dev.tx_bytes_id = ctr[1].id;
      g_strlcpy (dev.iface, name, sizeof dev.iface);

      g_array_append_val (devices, dev);
    }

  values = g_new0 (SysprofCaptureCounterValue, (devices->len*2) + 2);
  ids = g_new0 (guint, (devices->len*2) + 2);
  ids[0] = combined_rx_id;
  ids[1] = combined_tx_id;
  for (guint i = 0; i < devices->len; i++)
    {
      const DeviceUsage *dev = &g_array_index (devices, DeviceUsage, i);
      ids[(i+1)*2] = dev->rx_bytes_id;
      ids[(i+1)*2+1] = dev->tx_bytes_id;
    }

  for (;;)
    {
      DeviceUsage *first = &g_array_index (devices, DeviceUsage, 0);
      gint64 combined_rx = 0;
      gint64 combined_tx = 0;

      n_read = dex_await_int64 (dex_aio_read (NULL, stat_fd, buf->data, buf->len, 0), &error);
      if (n_read <= 0)
        break;

      lineno = 0;
      line_reader_init (&reader, (char *)buf->data, n_read);
      while ((line = line_reader_next (&reader, &line_len)))
        {
          DeviceUsage *dev;
          guint index;
          char *name;
          char *ptr = line;

          line[line_len] = 0;

          if (lineno++ < 2)
            continue;

          for (; *ptr && g_ascii_isspace (*ptr); ptr++) { /* Do Nothing */ }
          name = ptr;
          for (; *ptr && *ptr != ':'; ptr++) { /* Do Nothing */ }
          *ptr = 0;

          if (!(dev = find_device_by_name (devices, name)))
            continue;

          ptr++;

          sscanf (ptr,
                  "%"G_GINT64_FORMAT
                  " %"G_GINT64_FORMAT
                  " %"G_GINT64_FORMAT
                  " %"G_GINT64_FORMAT
                  " %"G_GINT64_FORMAT
                  " %"G_GINT64_FORMAT
                  " %"G_GINT64_FORMAT
                  " %"G_GINT64_FORMAT
                  " %"G_GINT64_FORMAT
                  " %"G_GINT64_FORMAT
                  " %"G_GINT64_FORMAT
                  " %"G_GINT64_FORMAT
                  " %"G_GINT64_FORMAT
                  " %"G_GINT64_FORMAT
                  " %"G_GINT64_FORMAT
                  " %"G_GINT64_FORMAT,
                  &dev->rx_bytes,
                  &dev->rx_packets,
                  &dev->rx_errors,
                  &dev->rx_dropped,
                  &dev->rx_fifo,
                  &dev->rx_frame,
                  &dev->rx_compressed,
                  &dev->rx_multicast,
                  &dev->tx_bytes,
                  &dev->tx_packets,
                  &dev->tx_errors,
                  &dev->tx_dropped,
                  &dev->tx_fifo,
                  &dev->tx_collisions,
                  &dev->tx_carrier,
                  &dev->tx_compressed);

          combined_rx += dev->rx_bytes;
          combined_tx += dev->tx_bytes;

          index = dev - first;

          values[(index+1)*2].v64 = dev->rx_bytes;
          values[(index+1)*2+1].v64 = dev->tx_bytes;
        }

      values[0].v64 = combined_rx;
      values[1].v64 = combined_tx;

      sysprof_capture_writer_set_counters (writer,
                                           SYSPROF_CAPTURE_CURRENT_TIME,
                                           -1,
                                           -1,
                                           ids,
                                           values,
                                           (devices->len + 1) * 2);

      /* Wait for cancellation or ½ second */
      dex_await (dex_future_first (dex_ref (record->cancellable),
                                   dex_timeout_new_usec (G_USEC_PER_SEC / 2),
                                   NULL),
                 NULL);
      if (dex_future_get_status (record->cancellable) != DEX_FUTURE_STATUS_PENDING)
        break;
    }

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
sysprof_network_usage_record (SysprofInstrument *instrument,
                              SysprofRecording  *recording,
                              GCancellable      *cancellable)
{
  Record *record;

  g_assert (SYSPROF_IS_NETWORK_USAGE (instrument));
  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (G_IS_CANCELLABLE (cancellable));

  record = g_new0 (Record, 1);
  record->recording = g_object_ref (recording);
  record->cancellable = dex_cancellable_new_from_cancellable (cancellable);

  return dex_scheduler_spawn (NULL, 0,
                              sysprof_network_usage_record_fiber,
                              record,
                              record_free);
}

static void
sysprof_network_usage_class_init (SysprofNetworkUsageClass *klass)
{
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  instrument_class->record = sysprof_network_usage_record;
}

static void
sysprof_network_usage_init (SysprofNetworkUsage *self)
{
}

SysprofInstrument *
sysprof_network_usage_new (void)
{
  return g_object_new (SYSPROF_TYPE_NETWORK_USAGE, NULL);
}
