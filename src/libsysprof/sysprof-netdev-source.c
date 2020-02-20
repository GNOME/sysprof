/* sysprof-netdev-source.c
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

#define G_LOG_DOMAIN "sysprof-netdev-source"

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sysprof-backport-autocleanups.h"
#include "sysprof-line-reader.h"
#include "sysprof-netdev-source.h"
#include "sysprof-helpers.h"

struct _SysprofNetdevSource
{
  GObject               parent_instance;

  SysprofCaptureWriter *writer;
  GArray               *netdevs;

  /* Combined (all devices) rx/tx counters */
  guint                 combined_rx_id;
  guint                 combined_tx_id;

  /* FD for /proc/net/dev contents */
  gint                  netdev_fd;

  /* GSource ID for polling */
  guint                 poll_source;
};

typedef struct
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
} Netdev;

static void source_iface_init (SysprofSourceInterface *);

G_DEFINE_TYPE_WITH_CODE (SysprofNetdevSource, sysprof_netdev_source, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (SYSPROF_TYPE_SOURCE, source_iface_init))

static Netdev *
find_device_by_name (SysprofNetdevSource *self,
                     const gchar         *name)
{
  g_assert (SYSPROF_IS_NETDEV_SOURCE (self));
  g_assert (self->writer != NULL);
  g_assert (name != NULL);

  for (guint i = 0; i < self->netdevs->len; i++)
    {
      Netdev *netdev = &g_array_index (self->netdevs, Netdev, i);

      if (strcmp (name, netdev->iface) == 0)
        return netdev;
    }

  return NULL;
}

static Netdev *
register_counters_by_name (SysprofNetdevSource *self,
                           const gchar         *name)
{
  SysprofCaptureCounter ctr[2] = {{{0}}};
  g_autofree gchar *rx = NULL;
  g_autofree gchar *tx = NULL;
  Netdev nd = {0};

  g_assert (SYSPROF_IS_NETDEV_SOURCE (self));
  g_assert (name != NULL);
  g_assert (self->writer != NULL);

  rx = g_strdup_printf ("RX Bytes (%s)", name);
  tx = g_strdup_printf ("TX Bytes (%s)", name);

  nd.rx_bytes_id = sysprof_capture_writer_request_counter (self->writer, 1);
  nd.tx_bytes_id = sysprof_capture_writer_request_counter (self->writer, 1);
  g_strlcpy (nd.iface, name, sizeof nd.iface);
  g_array_append_val (self->netdevs, nd);

  g_strlcpy (ctr[0].category, "Network", sizeof ctr[0].category);
  g_strlcpy (ctr[0].name, rx, sizeof ctr[0].name);
  g_strlcpy (ctr[0].description, name, sizeof ctr[0].description);
  ctr[0].id = nd.rx_bytes_id;
  ctr[0].type = SYSPROF_CAPTURE_COUNTER_INT64;
  ctr[0].value.v64 = 0;

  g_strlcpy (ctr[1].category, "Network", sizeof ctr[1].category);
  g_strlcpy (ctr[1].name, tx, sizeof ctr[1].name);
  g_strlcpy (ctr[1].description, name, sizeof ctr[1].description);
  ctr[1].id = nd.tx_bytes_id;
  ctr[1].type = SYSPROF_CAPTURE_COUNTER_INT64;
  ctr[1].value.v64 = 0;

  sysprof_capture_writer_define_counters (self->writer,
                                          SYSPROF_CAPTURE_CURRENT_TIME,
                                          -1,
                                          -1,
                                          ctr, G_N_ELEMENTS (ctr));

  return &g_array_index (self->netdevs, Netdev, self->netdevs->len - 1);
}

static gboolean
sysprof_netdev_source_get_is_ready (SysprofSource *source)
{
  return TRUE;
}

static void
sysprof_netdev_source_prepare (SysprofSource *source)
{
  SysprofNetdevSource *self = (SysprofNetdevSource *)source;
  g_autoptr(GError) error = NULL;
  SysprofCaptureCounter ctr[2] = {{{0}}};

  g_assert (SYSPROF_IS_NETDEV_SOURCE (self));

  self->netdev_fd = g_open ("/proc/net/dev", O_RDONLY, 0);

  if (self->netdev_fd == -1)
    {
      int errsv = errno;
      error = g_error_new (G_FILE_ERROR,
                           g_file_error_from_errno (errsv),
                           "%s",
                           g_strerror (errsv));
      sysprof_source_emit_failed (source, error);
      return;
    }

  self->combined_rx_id = sysprof_capture_writer_request_counter (self->writer, 1);
  self->combined_tx_id = sysprof_capture_writer_request_counter (self->writer, 1);

  g_strlcpy (ctr[0].category, "Network", sizeof ctr[0].category);
  g_strlcpy (ctr[0].name, "RX Bytes", sizeof ctr[0].name);
  g_strlcpy (ctr[0].description, "Combined", sizeof ctr[0].description);
  ctr[0].id = self->combined_rx_id;
  ctr[0].type = SYSPROF_CAPTURE_COUNTER_INT64;
  ctr[0].value.v64 = 0;

  g_strlcpy (ctr[1].category, "Network", sizeof ctr[1].category);
  g_strlcpy (ctr[1].name, "TX Bytes", sizeof ctr[1].name);
  g_strlcpy (ctr[1].description, "Combined", sizeof ctr[1].description);
  ctr[1].id = self->combined_tx_id;
  ctr[1].type = SYSPROF_CAPTURE_COUNTER_INT64;
  ctr[1].value.v64 = 0;

  sysprof_capture_writer_define_counters (self->writer,
                                          SYSPROF_CAPTURE_CURRENT_TIME,
                                          -1,
                                          -1,
                                          ctr, G_N_ELEMENTS (ctr));

  sysprof_source_emit_ready (source);
}

static gboolean
sysprof_netdev_source_poll_cb (gpointer data)
{
  g_autoptr(SysprofLineReader) reader = NULL;
  SysprofNetdevSource *self = data;
  g_autoptr(GArray) counters = NULL;
  g_autoptr(GArray) values = NULL;
  gchar buf[4096*4];
  gint64 combined_rx = 0;
  gint64 combined_tx = 0;
  gssize len;
  gsize line_len;
  gchar *line;

  g_assert (SYSPROF_IS_NETDEV_SOURCE (self));

  if (self->netdev_fd == -1)
    {
      self->poll_source = 0;
      return G_SOURCE_REMOVE;
    }

  /* Seek to 0 forces reload of data */
  lseek (self->netdev_fd, 0, SEEK_SET);

  len = read (self->netdev_fd, buf, sizeof buf - 1);

  /* Bail for now unless we read enough data */
  if (len > 0)
    buf[len] = 0;
  else
    return G_SOURCE_CONTINUE;

  counters = g_array_new (FALSE, FALSE, sizeof (guint));
  values = g_array_new (FALSE, FALSE, sizeof (SysprofCaptureCounterValue));
  reader = sysprof_line_reader_new (buf, len);

#if 0
Entries looks like this...
------------------------------------------------------------------------------------------------------------------------------
Inter-|   Receive                                                |  Transmit
 face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed
    lo:   10410     104    0    0    0     0          0         0    10410     104    0    0    0     0       0          0
  eth0:1069675772 4197670    0    0    0     0          0         0 3221945712 3290571    0    0    0     0       0          0
------------------------------------------------------------------------------------------------------------------------------
#endif

  /* Skip first two lines */
  for (guint i = 0; i < 2; i++)
    {
      if (!(line = (gchar *)sysprof_line_reader_next (reader, &line_len)))
        return G_SOURCE_CONTINUE;
    }

  while ((line = (gchar *)sysprof_line_reader_next (reader, &line_len)))
    {
      Netdev *nd;
      gchar *name;
      gchar *ptr = line;

      line[line_len] = 0;

      for (; *ptr && g_ascii_isspace (*ptr); ptr++) { /* Do Nothing */ }
      name = ptr;
      for (; *ptr && *ptr != ':'; ptr++) { /* Do Nothing */ }
      *ptr = 0;

      if (!(nd = find_device_by_name (self, name)))
        nd = register_counters_by_name (self, name);

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
              &nd->rx_bytes,
              &nd->rx_packets,
              &nd->rx_errors,
              &nd->rx_dropped,
              &nd->rx_fifo,
              &nd->rx_frame,
              &nd->rx_compressed,
              &nd->rx_multicast,
              &nd->tx_bytes,
              &nd->tx_packets,
              &nd->tx_errors,
              &nd->tx_dropped,
              &nd->tx_fifo,
              &nd->tx_collisions,
              &nd->tx_carrier,
              &nd->tx_compressed);

      combined_rx += nd->rx_bytes;
      combined_tx += nd->tx_bytes;

      g_array_append_val (counters, nd->rx_bytes_id);
      g_array_append_val (values, nd->rx_bytes);

      g_array_append_val (counters, nd->tx_bytes_id);
      g_array_append_val (values, nd->tx_bytes);
    }

  g_array_append_val (counters, self->combined_rx_id);
  g_array_append_val (values, combined_rx);

  g_array_append_val (counters, self->combined_tx_id);
  g_array_append_val (values, combined_tx);

  sysprof_capture_writer_set_counters (self->writer,
                                       SYSPROF_CAPTURE_CURRENT_TIME,
                                       -1,
                                       -1,
                                       (const guint *)(gpointer)counters->data,
                                       (const SysprofCaptureCounterValue *)(gpointer)values->data,
                                       counters->len);

  return G_SOURCE_CONTINUE;
}

static void
sysprof_netdev_source_start (SysprofSource *source)
{
  SysprofNetdevSource *self = (SysprofNetdevSource *)source;

  g_assert (SYSPROF_IS_NETDEV_SOURCE (self));

  self->poll_source = g_timeout_add (200, sysprof_netdev_source_poll_cb, self);

  /* Poll immediately */
  sysprof_netdev_source_poll_cb (self);
}

static void
sysprof_netdev_source_stop (SysprofSource *source)
{
  SysprofNetdevSource *self = (SysprofNetdevSource *)source;

  g_assert (SYSPROF_IS_NETDEV_SOURCE (self));

  /* Poll one last time */
  sysprof_netdev_source_poll_cb (self);

  g_clear_handle_id (&self->poll_source, g_source_remove);

  sysprof_source_emit_finished (source);
}

static void
sysprof_netdev_source_set_writer (SysprofSource        *source,
                                  SysprofCaptureWriter *writer)
{
  SysprofNetdevSource *self = (SysprofNetdevSource *)source;

  g_assert (SYSPROF_IS_NETDEV_SOURCE (self));
  g_assert (writer != NULL);

  g_clear_pointer (&self->writer, sysprof_capture_writer_unref);
  self->writer = sysprof_capture_writer_ref (writer);
}

static void
source_iface_init (SysprofSourceInterface *iface)
{
  iface->get_is_ready = sysprof_netdev_source_get_is_ready;
  iface->prepare = sysprof_netdev_source_prepare;
  iface->set_writer = sysprof_netdev_source_set_writer;
  iface->start = sysprof_netdev_source_start;
  iface->stop = sysprof_netdev_source_stop;
}

static void
sysprof_netdev_source_finalize (GObject *object)
{
  SysprofNetdevSource *self = (SysprofNetdevSource *)object;

  g_clear_pointer (&self->netdevs, g_array_unref);
  g_clear_pointer (&self->writer, sysprof_capture_writer_unref);

  if (self->netdev_fd != -1)
    {
      close (self->netdev_fd);
      self->netdev_fd = -1;
    }

  G_OBJECT_CLASS (sysprof_netdev_source_parent_class)->finalize (object);
}

static void
sysprof_netdev_source_class_init (SysprofNetdevSourceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_netdev_source_finalize;
}

static void
sysprof_netdev_source_init (SysprofNetdevSource *self)
{
  self->netdevs = g_array_new (FALSE, FALSE, sizeof (Netdev));
}

SysprofSource *
sysprof_netdev_source_new (void)
{
  return g_object_new (SYSPROF_TYPE_NETDEV_SOURCE, NULL);
}
