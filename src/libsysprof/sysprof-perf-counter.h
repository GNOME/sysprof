/* sysprof-perf-counter.h
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
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

#if !defined (SYSPROF_INSIDE) && !defined (SYSPROF_COMPILATION)
# error "Only <sysprof.h> can be included directly."
#endif

#include <gio/gio.h>
#include <linux/perf_event.h>


G_BEGIN_DECLS

/* Structs representing the layouts of perf records returned by the
 * kernel.
 *
 * perf returns variable-layout structs based on the
 * perf_event_sample_format selectors in perf_event_attr.sample_type.
 * These structs are the particular layouts that sysprof requests.
 */

#define SYSPROF_TYPE_PERF_COUNTER (sysprof_perf_counter_get_type())

typedef struct _SysprofPerfCounter SysprofPerfCounter;

#pragma pack(push, 1)

typedef struct
{
  struct perf_event_header header;
  guint32 pid;
  guint32 ppid;
  guint32 tid;
  guint32 ptid;
  guint64 time;
} SysprofPerfCounterEventFork;

typedef struct
{
  struct perf_event_header header;
  guint32 pid;
  guint32 tid;
  gchar comm[0];
} SysprofPerfCounterEventComm;

typedef struct
{
  struct perf_event_header header;
  guint32 pid;
  guint32 ppid;
  guint32 tid;
  guint32 ptid;
  guint64 time;
} SysprofPerfCounterEventExit;

typedef struct
{
  struct perf_event_header header;
  guint32 pid;
  guint32 tid;
  guint64 addr;
  guint64 len;
  guint64 pgoff;
  char filename[0];
} SysprofPerfCounterEventMmap;

typedef struct
{
  struct perf_event_header header;
  guint64 identifier;
  guint64 ip;
  guint32 pid;
  guint32 tid;
  guint64 time;
  guint64 n_ips;
  guint64 ips[0];
} SysprofPerfCounterEventCallchain;

typedef struct
{
  struct perf_event_header header;
  guint64 identifier;
  guint64 ip;
  guint32 pid;
  guint32 tid;
  guint64 time;
  guint32 raw_size;
  guchar raw[];
} SysprofPerfCounterEventTracepoint;

typedef union
{
  struct perf_event_header          header;
  guint8                            raw[0];
  SysprofPerfCounterEventFork       fork;
  SysprofPerfCounterEventComm       comm;
  SysprofPerfCounterEventExit       exit;
  SysprofPerfCounterEventMmap       mmap;
  SysprofPerfCounterEventCallchain  callchain;
  SysprofPerfCounterEventTracepoint tracepoint;
} SysprofPerfCounterEvent;

#pragma pack(pop)

typedef void (*SysprofPerfCounterCallback) (SysprofPerfCounterEvent *event,
                                            guint                    cpu,
                                            gpointer                 user_data);

GType               sysprof_perf_counter_get_type         (void);
SysprofPerfCounter *sysprof_perf_counter_new              (GMainContext                *context);
void                sysprof_perf_counter_set_callback     (SysprofPerfCounter          *self,
                                                           SysprofPerfCounterCallback   callback,
                                                           gpointer                     callback_data,
                                                           GDestroyNotify               callback_data_destroy);
void                sysprof_perf_counter_add_pid          (SysprofPerfCounter          *self,
                                                           GPid                         pid);
gint                sysprof_perf_counter_open             (SysprofPerfCounter          *self,
                                                           struct perf_event_attr      *attr,
                                                           GPid                         pid,
                                                           gint                         cpu,
                                                           gint                         group_fd,
                                                           gulong                       flags);
void                sysprof_perf_counter_take_fd          (SysprofPerfCounter          *self,
                                                           int                          fd);
void                sysprof_perf_counter_enable           (SysprofPerfCounter          *self);
void                sysprof_perf_counter_disable          (SysprofPerfCounter          *self);
void                sysprof_perf_counter_close            (SysprofPerfCounter          *self,
                                                           gint                         fd);
SysprofPerfCounter *sysprof_perf_counter_ref              (SysprofPerfCounter          *self);
void                sysprof_perf_counter_unref            (SysprofPerfCounter          *self);

G_END_DECLS
