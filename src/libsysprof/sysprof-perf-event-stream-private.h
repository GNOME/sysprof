/* sysprof-perf-event-stream-private.h
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

#pragma once

#include <gio/gio.h>
#include <libdex.h>
#include <linux/perf_event.h>

#include <sysprof-capture.h>

G_BEGIN_DECLS

SYSPROF_ALIGNED_BEGIN(1)
typedef struct _SysprofPerfEventFork
{
  struct perf_event_header header;
  guint32 pid;
  guint32 ppid;
  guint32 tid;
  guint32 ptid;
  guint64 time;
} SysprofPerfEventFork
SYSPROF_ALIGNED_END(1);

SYSPROF_ALIGNED_BEGIN(1)
typedef struct _SysprofPerfEventComm
{
  struct perf_event_header header;
  guint32 pid;
  guint32 tid;
  gchar comm[0];
} SysprofPerfEventComm
SYSPROF_ALIGNED_END(1);

SYSPROF_ALIGNED_BEGIN(1)
typedef struct _SysprofPerfEventExit
{
  struct perf_event_header header;
  guint32 pid;
  guint32 ppid;
  guint32 tid;
  guint32 ptid;
  guint64 time;
} SysprofPerfEventExit
SYSPROF_ALIGNED_END(1);

SYSPROF_ALIGNED_BEGIN(1)
typedef struct _SysprofPerfEventMmap
{
  struct perf_event_header header;
  guint32 pid;
  guint32 tid;
  guint64 addr;
  guint64 len;
  guint64 pgoff;
  char filename[0];
} SysprofPerfEventMmap
SYSPROF_ALIGNED_END(1);

SYSPROF_ALIGNED_BEGIN(1)
typedef struct _SysprofPerfEventMmap2
{
  struct perf_event_header header;
  guint32 pid;
  guint32 tid;
  guint64 addr;
  guint64 len;
  guint64 pgoff;
  union {
    struct {
      guint32 maj;
      guint32 min;
      guint64 ino;
      guint64 ino_generation;
    };
    struct {
      guint8  build_id_size;
      guint8  __reserved_1;
      guint16 __reserved_2;
      guint8  build_id[20];
    };
  };
  guint32 prot;
  guint32 flags;
  char filename[0];
} SysprofPerfEventMmap2
SYSPROF_ALIGNED_END(1);

SYSPROF_ALIGNED_BEGIN(1)
typedef struct _SysprofPerfEventCallchain
{
  struct perf_event_header header;
  guint64 identifier;
  guint64 ip;
  guint32 pid;
  guint32 tid;
  guint64 time;
  guint64 n_ips;
  guint64 ips[0];
} SysprofPerfEventCallchain
SYSPROF_ALIGNED_END(1);

SYSPROF_ALIGNED_BEGIN(1)
typedef struct _SysprofPerfEventTracepoint
{
  struct perf_event_header header;
  guint64 identifier;
  guint64 ip;
  guint32 pid;
  guint32 tid;
  guint64 time;
  guint32 raw_size;
  guchar raw[];
} SysprofPerfEventTracepoint
SYSPROF_ALIGNED_END(1);

SYSPROF_ALIGNED_BEGIN(1)
typedef struct _SysprofPerfEventLost
{
  struct perf_event_header header;
  guint64 identifier;
  guint64 lost;
} SysprofPerfEventLost
SYSPROF_ALIGNED_END(1);

SYSPROF_ALIGNED_BEGIN(1)
typedef union _SysprofPerfEvent
{
  struct perf_event_header   header;
  SysprofPerfEventFork       fork;
  SysprofPerfEventComm       comm;
  SysprofPerfEventExit       exit;
  SysprofPerfEventMmap       mmap;
  SysprofPerfEventMmap2      mmap2;
  SysprofPerfEventCallchain  callchain;
  SysprofPerfEventTracepoint tracepoint;
  SysprofPerfEventLost       lost;
  guint8                     raw[0];
} SysprofPerfEvent
SYSPROF_ALIGNED_END(1);

typedef void (*SysprofPerfEventCallback) (const SysprofPerfEvent *event,
                                          guint                   cpu,
                                          gpointer                user_data);

#define SYSPROF_TYPE_PERF_EVENT_STREAM (sysprof_perf_event_stream_get_type())

G_DECLARE_FINAL_TYPE (SysprofPerfEventStream, sysprof_perf_event_stream, SYSPROF, PERF_EVENT_STREAM, GObject)

DexFuture *sysprof_perf_event_stream_new       (GDBusConnection               *connection,
                                                struct perf_event_attr        *attr,
                                                int                            cpu,
                                                int                            group_fd,
                                                guint64                        flags,
                                                SysprofPerfEventCallback       callback,
                                                gpointer                       callback_data,
                                                GDestroyNotify                 callback_data_destroy);
gboolean   sysprof_perf_event_stream_enable    (SysprofPerfEventStream        *self,
                                                GError                       **error);
gboolean   sysprof_perf_event_stream_disable   (SysprofPerfEventStream        *self,
                                                GError                       **error);
GVariant  *_sysprof_perf_event_attr_to_variant (const struct perf_event_attr  *attr);

G_END_DECLS
