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

#include <glib-object.h>
#include <linux/perf_event.h>


G_BEGIN_DECLS

#define SP_TYPE_PERF_COUNTER (sp_perf_counter_get_type())

typedef struct _SpPerfCounter SpPerfCounter;

#pragma pack(push, 1)

typedef struct
{
  /*
   * These fields are available as the suffix only because we have specified
   * them when creating attributes. Be careful about using them.
   * Ideally, we would probably switch from using structures overlaid with
   * casts to a reader design, which knows about the attributes.
   */
  guint32 pid, tid;
  guint64 time;
} SpPerfCounterSuffix;

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
