/* sp-perf-counter.h
 *
 * Copyright (C) 2016 Christian Hergert <chergert@redhat.com>
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
 */

#ifndef SP_PERF_COUNTER_H
#define SP_PERF_COUNTER_H

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

#define SP_TYPE_PERF_COUNTER (sp_perf_counter_get_type())

typedef struct _SpPerfCounter SpPerfCounter;

#pragma pack(push, 1)

typedef struct
{
  struct perf_event_header header;
  guint32 pid;
  guint32 ppid;
  guint32 tid;
  guint32 ptid;
  guint64 time;
} SpPerfCounterEventFork;

typedef struct
{
  struct perf_event_header header;
  guint32 pid;
  guint32 tid;
  gchar comm[0];
} SpPerfCounterEventComm;

typedef struct
{
  struct perf_event_header header;
  guint32 pid;
  guint32 ppid;
  guint32 tid;
  guint32 ptid;
  guint64 time;
} SpPerfCounterEventExit;

typedef struct
{
  struct perf_event_header header;
  guint32 pid;
  guint32 tid;
  guint64 addr;
  guint64 len;
  guint64 pgoff;
  char filename[0];
} SpPerfCounterEventMmap;

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
} SpPerfCounterEventSample;

typedef union
{
  struct perf_event_header header;
  guint8                   raw[0];
  SpPerfCounterEventFork   fork;
  SpPerfCounterEventComm   comm;
  SpPerfCounterEventExit   exit;
  SpPerfCounterEventMmap   mmap;
  SpPerfCounterEventSample sample;
} SpPerfCounterEvent;

#pragma pack(pop)

typedef void (*SpPerfCounterCallback) (SpPerfCounterEvent *event,
                                       guint               cpu,
                                       gpointer            user_data);

void     sp_perf_counter_authorize_async  (GCancellable         *cancellable,
                                           GAsyncReadyCallback   callback,
                                           gpointer              user_data);
gboolean sp_perf_counter_authorize_finish (GAsyncResult         *result,
                                           GError              **error);

GType          sp_perf_counter_get_type     (void);
SpPerfCounter *sp_perf_counter_new          (GMainContext           *context);
void           sp_perf_counter_set_callback (SpPerfCounter          *self,
                                             SpPerfCounterCallback   callback,
                                             gpointer                callback_data,
                                             GDestroyNotify          callback_data_destroy);
void           sp_perf_counter_add_pid      (SpPerfCounter          *self,
                                             GPid                    pid);
gint           sp_perf_counter_open         (SpPerfCounter          *self,
                                             struct perf_event_attr *attr,
                                             GPid                    pid,
                                             gint                    cpu,
                                             gint                    group_fd,
                                             gulong                  flags);
void           sp_perf_counter_take_fd      (SpPerfCounter          *self,
                                             int                     fd);
void           sp_perf_counter_enable       (SpPerfCounter          *self);
void           sp_perf_counter_disable      (SpPerfCounter          *self);
void           sp_perf_counter_close        (SpPerfCounter          *self,
                                             gint                    fd);
SpPerfCounter *sp_perf_counter_ref          (SpPerfCounter          *self);
void           sp_perf_counter_unref        (SpPerfCounter          *self);

G_END_DECLS

#endif /* SP_PERF_COUNTER_H */
