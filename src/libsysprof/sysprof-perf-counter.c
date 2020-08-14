/* sysprof-perf-counter.c
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

/* Sysprof -- Sampling, systemwide CPU profiler
 * Copyright 2004, Red Hat, Inc.
 * Copyright 2004, 2005, Soeren Sandmann
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <errno.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#ifdef HAVE_STDATOMIC_H
# include <stdatomic.h>
#endif
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "sysprof-helpers.h"
#include "sysprof-perf-counter.h"

/*
 * Number of pages to map for the ring buffer.  We map one additional buffer
 * at the beginning for header information to communicate with perf.
 */
#define N_PAGES 32

/*
 * This represents a stream coming to us from perf. All SysprofPerfCounterInfo
 * share a single GSource used for watching incoming G_IO_IN requests.
 * The map is the mmap() zone we are using as a ring buffer for communicating
 * with perf. The rest is for managing the ring buffer.
 */
typedef struct
{
  gint                         fd;
  gpointer                     fdtag;
  struct perf_event_mmap_page *map;
  guint8                      *data;
  guint64                      tail;
  gint                         cpu;
  guint                        in_callback : 1;
} SysprofPerfCounterInfo;

struct _SysprofPerfCounter
{
  volatile gint ref_count;

  /*
   * If we are should currently be enabled. We allow calling
   * multiple times and disabling when we reach zero.
   */
  guint enabled;

  /*
   * Our main context and source for delivering callbacks.
   */
  GMainContext *context;
  GSource *source;

  /*
   * An array of SysprofPerfCounterInfo, indicating all of our open
   * perf stream.s
   */
  GPtrArray *info;

  /*
   * The callback to execute for every discovered perf event.
   */
  SysprofPerfCounterCallback callback;
  gpointer callback_data;
  GDestroyNotify callback_data_destroy;

  /*
   * The number of samples we've recorded.
   */
  guint64 n_samples;
};

typedef struct
{
  GSource             source;
  SysprofPerfCounter *counter;
} PerfGSource;

G_DEFINE_BOXED_TYPE (SysprofPerfCounter,
                     sysprof_perf_counter,
                     (GBoxedCopyFunc)sysprof_perf_counter_ref,
                     (GBoxedFreeFunc)sysprof_perf_counter_unref)

static gboolean
perf_gsource_dispatch (GSource     *source,
                       GSourceFunc  callback,
                       gpointer     user_data)
{
  return callback ? callback (user_data) : G_SOURCE_CONTINUE;
}

static GSourceFuncs source_funcs = {
  NULL, NULL, perf_gsource_dispatch, NULL
};

static void
sysprof_perf_counter_info_free (SysprofPerfCounterInfo *info)
{
  if (info->map)
    {
      gsize map_size;

      map_size = N_PAGES * getpagesize () + getpagesize ();
      munmap (info->map, map_size);

      info->map = NULL;
      info->data = NULL;
    }

  if (info->fd != -1)
    {
      close (info->fd);
      info->fd = 0;
    }

  g_slice_free (SysprofPerfCounterInfo, info);
}

static void
sysprof_perf_counter_finalize (SysprofPerfCounter *self)
{
  guint i;

  g_assert (self != NULL);
  g_assert (self->ref_count == 0);

  for (i = 0; i < self->info->len; i++)
    {
      SysprofPerfCounterInfo *info = g_ptr_array_index (self->info, i);

      if (info->fdtag)
        g_source_remove_unix_fd (self->source, info->fdtag);

      sysprof_perf_counter_info_free (info);
    }

  if (self->callback_data_destroy)
    self->callback_data_destroy (self->callback_data);

  g_clear_pointer (&self->source, g_source_destroy);
  g_clear_pointer (&self->info, g_ptr_array_unref);
  g_clear_pointer (&self->context, g_main_context_unref);
  g_slice_free (SysprofPerfCounter, self);
}

void
sysprof_perf_counter_unref (SysprofPerfCounter *self)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (self->ref_count > 0);

  if (g_atomic_int_dec_and_test (&self->ref_count))
    sysprof_perf_counter_finalize (self);
}

SysprofPerfCounter *
sysprof_perf_counter_ref (SysprofPerfCounter *self)
{
  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (self->ref_count > 0, NULL);

  g_atomic_int_inc (&self->ref_count);

  return self;
}

static void
sysprof_perf_counter_flush (SysprofPerfCounter     *self,
                            SysprofPerfCounterInfo *info)
{
  guint64 head;
  guint64 tail;
  guint64 n_bytes = N_PAGES * getpagesize ();
  guint64 mask = n_bytes - 1;

  g_assert (self != NULL);
  g_assert (info != NULL);

  tail = info->tail;
  head = info->map->data_head;

#ifdef HAVE_STDATOMIC_H
  atomic_thread_fence (memory_order_acquire);
#elif G_GNUC_CHECK_VERSION(3, 0)
  __sync_synchronize ();
#endif

  if (head < tail)
    tail = head;

  while ((head - tail) >= sizeof (struct perf_event_header))
    {
      g_autofree guint8 *free_me = NULL;
      struct perf_event_header *header;
      guint8 buffer[4096];

      /* Note that:
       *
       * - perf events are a multiple of 64 bits
       * - the perf event header is 64 bits
       * - the data area is a multiple of 64 bits
       *
       * which means there will always be space for one header, which means we
       * can safely dereference the size field.
       */
      header = (struct perf_event_header *)(gpointer)(info->data + (tail & mask));

      if (header->size > head - tail)
        {
          /* The kernel did not generate a complete event.
           * I don't think that can happen, but we may as well
           * be paranoid.
           */
          break;
        }

      if (info->data + (tail & mask) + header->size > info->data + n_bytes)
        {
          gint n_before;
          gint n_after;
          guint8 *b;

          if (header->size > sizeof buffer)
            free_me = b = g_malloc (header->size);
          else
            b = buffer;

          n_after = (tail & mask) + header->size - n_bytes;
          n_before = header->size - n_after;

          memcpy (b, info->data + (tail & mask), n_before);
          memcpy (b + n_before, info->data, n_after);

          header = (struct perf_event_header *)(gpointer)b;
        }

      if (header->type == PERF_RECORD_SAMPLE)
        self->n_samples++;

      if (self->callback != NULL)
        {
          info->in_callback = TRUE;
          self->callback ((SysprofPerfCounterEvent *)header, info->cpu, self->callback_data);
          info->in_callback = FALSE;
        }

      tail += header->size;
    }

  info->tail = tail;

#ifdef HAVE_STDATOMIC_H
  atomic_thread_fence (memory_order_seq_cst);
#elif G_GNUC_CHECK_VERSION(3, 0)
  __sync_synchronize ();
#endif

  info->map->data_tail = tail;
}

static gboolean
sysprof_perf_counter_dispatch (gpointer user_data)
{
  SysprofPerfCounter *self = user_data;

  g_assert (self != NULL);
  g_assert (self->info != NULL);

  for (guint i = 0; i < self->info->len; i++)
    {
      SysprofPerfCounterInfo *info = g_ptr_array_index (self->info, i);

      sysprof_perf_counter_flush (self, info);
    }

  return G_SOURCE_CONTINUE;
}

static void
sysprof_perf_counter_enable_info (SysprofPerfCounter     *self,
                                  SysprofPerfCounterInfo *info)
{
  g_assert (self != NULL);
  g_assert (info != NULL);

  if (0 != ioctl (info->fd, PERF_EVENT_IOC_ENABLE))
    g_warning ("Failed to enable counters");

  g_source_modify_unix_fd (self->source, info->fdtag, G_IO_IN);
}

SysprofPerfCounter *
sysprof_perf_counter_new (GMainContext *context)
{
  SysprofPerfCounter *ret;

  if (context == NULL)
    context = g_main_context_default ();

  ret = g_slice_new0 (SysprofPerfCounter);
  ret->ref_count = 1;
  ret->info = g_ptr_array_new ();
  ret->context = g_main_context_ref (context);
  ret->source = g_source_new (&source_funcs, sizeof (PerfGSource));

  ((PerfGSource *)ret->source)->counter = ret;
  g_source_set_callback (ret->source, sysprof_perf_counter_dispatch, ret, NULL);
  g_source_set_name (ret->source, "[perf]");
  g_source_attach (ret->source, context);

  return ret;
}

void
sysprof_perf_counter_close (SysprofPerfCounter *self,
                            gint                fd)
{
  guint i;

  g_return_if_fail (self != NULL);
  g_return_if_fail (fd != -1);

  for (i = 0; i < self->info->len; i++)
    {
      SysprofPerfCounterInfo *info = g_ptr_array_index (self->info, i);

      if (info->fd == fd)
        {
          g_ptr_array_remove_index (self->info, i);
          if (self->source)
            g_source_remove_unix_fd (self->source, info->fdtag);
          sysprof_perf_counter_info_free (info);
          return;
        }
    }
}

static void
sysprof_perf_counter_add_info (SysprofPerfCounter *self,
                               int                 fd,
                               int                 cpu)
{
  SysprofPerfCounterInfo *info;
  guint8 *map;
  gsize map_size;

  g_assert (self != NULL);
  g_assert (fd != -1);

  map_size = N_PAGES * getpagesize () + getpagesize ();
  map = mmap (NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

  if (map == MAP_FAILED)
    {
      close (fd);
      return;
    }

  info = g_slice_new0 (SysprofPerfCounterInfo);
  info->fd = fd;
  info->map = (gpointer)map;
  info->data = map + getpagesize ();
  info->tail = 0;
  info->cpu = cpu;

  g_ptr_array_add (self->info, info);

  info->fdtag = g_source_add_unix_fd (self->source, info->fd, G_IO_ERR);

  if (self->enabled)
    sysprof_perf_counter_enable_info (self, info);
}

void
sysprof_perf_counter_take_fd (SysprofPerfCounter *self,
                              int                 fd)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (fd > -1);

  sysprof_perf_counter_add_info (self, fd, -1);
}

gint
sysprof_perf_counter_open (SysprofPerfCounter     *self,
                           struct perf_event_attr *attr,
                           GPid                    pid,
                           gint                    cpu,
                           gint                    group_fd,
                           gulong                  flags)
{
  SysprofHelpers *helpers = sysprof_helpers_get_default ();
  gint out_fd = -1;

  g_return_val_if_fail (self != NULL, -1);
  g_return_val_if_fail (attr != NULL, -1);
  g_return_val_if_fail (cpu >= -1, -1);
  g_return_val_if_fail (pid >= -1, -1);
  g_return_val_if_fail (group_fd >= -1, -1);

  if (sysprof_helpers_perf_event_open (helpers, attr, pid, cpu, group_fd, flags, NULL, &out_fd, NULL))
    {
      sysprof_perf_counter_take_fd (self, out_fd);
      return out_fd;
    }

  return -1;
}

void
sysprof_perf_counter_set_callback (SysprofPerfCounter         *self,
                                   SysprofPerfCounterCallback  callback,
                                   gpointer                    callback_data,
                                   GDestroyNotify              callback_data_destroy)
{
  g_return_if_fail (self != NULL);

  if (self->callback_data_destroy)
    self->callback_data_destroy (self->callback_data);

  self->callback = callback;
  self->callback_data = callback_data;
  self->callback_data_destroy = callback_data_destroy;
}

void
sysprof_perf_counter_enable (SysprofPerfCounter *self)
{
  g_return_if_fail (self != NULL);

  if (g_atomic_int_add (&self->enabled, 1) == 0)
    {
      for (guint i = 0; i < self->info->len; i++)
        {
          SysprofPerfCounterInfo *info = g_ptr_array_index (self->info, i);

          sysprof_perf_counter_enable_info (self, info);
        }
    }
}

void
sysprof_perf_counter_disable (SysprofPerfCounter *self)
{
  g_return_if_fail (self != NULL);

  if (g_atomic_int_dec_and_test (&self->enabled))
    {
      for (guint i = 0; i < self->info->len; i++)
        {
          SysprofPerfCounterInfo *info = g_ptr_array_index (self->info, i);

          if (0 != ioctl (info->fd, PERF_EVENT_IOC_DISABLE))
            g_warning ("Failed to disable counters");

          if (!info->in_callback)
            sysprof_perf_counter_flush (self, info);

          g_source_modify_unix_fd (self->source, info->fdtag, G_IO_ERR);
        }
    }
}
