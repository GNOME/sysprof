/* mapped-ring-buffer-source.c
 *
 * Copyright 2020 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sysprof-mapped-ring-buffer-source"

#include "config.h"

#include <glib.h>

#include "mapped-ring-buffer-source.h"

typedef struct _MappedRingSource
{
  GSource           source;
  MappedRingBuffer *buffer;
} MappedRingSource;

static gboolean
mapped_ring_source_dispatch (GSource     *source,
                             GSourceFunc  callback,
                             gpointer     user_data)
{
  MappedRingSource *real_source = (MappedRingSource *)source;

  g_assert (source != NULL);

  return mapped_ring_buffer_drain (real_source->buffer,
                                   (MappedRingBufferCallback)callback,
                                   user_data);
}

static void
mapped_ring_source_finalize (GSource *source)
{
  MappedRingSource *real_source = (MappedRingSource *)source;

  if (real_source != NULL)
    g_clear_pointer (&real_source->buffer, mapped_ring_buffer_unref);
}

static gboolean
mapped_ring_source_check (GSource *source)
{
  MappedRingSource *real_source = (MappedRingSource *)source;

  g_assert (real_source != NULL);
  g_assert (real_source->buffer != NULL);

  return !mapped_ring_buffer_is_empty (real_source->buffer);
}

static gboolean
mapped_ring_source_prepare (GSource *source,
                            gint    *timeout_)
{
  MappedRingSource *real_source = (MappedRingSource *)source;

  g_assert (real_source != NULL);
  g_assert (real_source->buffer != NULL);

  if (!mapped_ring_buffer_is_empty (real_source->buffer))
    return TRUE;

  *timeout_ = 5;

  return FALSE;
}

static GSourceFuncs mapped_ring_source_funcs = {
  .prepare  = mapped_ring_source_prepare,
  .check    = mapped_ring_source_check,
  .dispatch = mapped_ring_source_dispatch,
  .finalize = mapped_ring_source_finalize,
};

guint
mapped_ring_buffer_create_source_full (MappedRingBuffer         *self,
                                       MappedRingBufferCallback  source_func,
                                       gpointer                  user_data,
                                       GDestroyNotify            destroy)
{
  MappedRingSource *source;
  guint ret;

  g_return_val_if_fail (self != NULL, 0);
  g_return_val_if_fail (source_func != NULL, 0);

  source = (MappedRingSource *)g_source_new (&mapped_ring_source_funcs, sizeof (MappedRingSource));
  source->buffer = mapped_ring_buffer_ref (self);
  g_source_set_callback ((GSource *)source, (GSourceFunc)source_func, user_data, destroy);
  g_source_set_name ((GSource *)source, "MappedRingSource");
  ret = g_source_attach ((GSource *)source, g_main_context_default ());
  g_source_unref ((GSource *)source);

  return ret;
}

guint
mapped_ring_buffer_create_source (MappedRingBuffer         *self,
                                  MappedRingBufferCallback  source_func,
                                  gpointer                  user_data)
{
  return mapped_ring_buffer_create_source_full (self, source_func, user_data, NULL);
}
