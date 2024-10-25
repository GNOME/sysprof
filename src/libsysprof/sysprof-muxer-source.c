/*
 * sysprof-muxer-source.c
 *
 * Copyright 2024 Christian Hergert <chergert@redhat.com>
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

#include <glib/gstdio.h>
#include <glib-unix.h>

#include "sysprof-muxer-source.h"

#define DEFAULT_BUFFER_SIZE (4096*16)

typedef struct _SysprofMuxerSource
{
  GSource               gsource;
  int                   capture_fd;
  SysprofCaptureWriter *writer;
  struct {
    guint8 *allocation;
    guint8 *begin;
    guint8 *end;
    guint8 *capacity;
    gsize   to_skip;
  } buffer;
} SysprofMuxerSource;

static gboolean
sysprof_muxer_source_size (SysprofMuxerSource *source)
{
  return source->buffer.end - source->buffer.begin;
}

static gboolean
sysprof_muxer_source_dispatch (GSource     *gsource,
                               GSourceFunc  callback,
                               gpointer     user_data)
{
  SysprofMuxerSource *source = (SysprofMuxerSource *)gsource;
  gssize n_read;

  g_assert (source != NULL);
  g_assert (source->writer != NULL);

  /* Try to read the next chunk */
  n_read = read (source->capture_fd, source->buffer.end, source->buffer.capacity - source->buffer.end);

  if (n_read > 0)
    {
      const SysprofCaptureFrame *frame;

      /* Advance tail to what was filled */
      source->buffer.end += n_read;

      /* Get to next alignment */
      if (source->buffer.to_skip)
        {
          gsize amount = MIN (source->buffer.to_skip, source->buffer.end - source->buffer.begin);
          source->buffer.begin += amount;
          source->buffer.to_skip -= amount;
        }

      /* If there is enough to read the frame header, try to read and dispatch
       * it in raw form. We assume we're the same endianness here because this
       * is coming from the same host (live-unwinder currently).
       */
      while (sysprof_muxer_source_size (source) >= sizeof *frame)
        {
          frame = (const SysprofCaptureFrame *)source->buffer.begin;

          if (frame->len <= sysprof_muxer_source_size (source))
            {
              source->buffer.begin += frame->len;

              if (frame->len % sizeof (guint64) != 0)
                source->buffer.to_skip = sizeof (guint64) - (frame->len % sizeof (guint64));

              /* TODO: Technically for counters/JIT map we need to translate them. */

              _sysprof_capture_writer_add_raw (source->writer, frame);
            }

          if (source->buffer.to_skip > 0 &&
              source->buffer.to_skip <= sysprof_muxer_source_size (source))
            {
              source->buffer.begin += source->buffer.to_skip;
              source->buffer.to_skip = 0;
              continue;
            }

          break;
        }

      /* Move anything left to the head of the buffer so we can
       * fill in the entire next frame of data.
       */
      if (source->buffer.begin < source->buffer.end)
        {
          /* TODO: Should we adjust for alignment here? */

          memmove (source->buffer.allocation,
                   source->buffer.begin,
                   source->buffer.end - source->buffer.begin);
          source->buffer.end = source->buffer.allocation + (source->buffer.end - source->buffer.begin);
          source->buffer.begin = source->buffer.allocation;
        }
      else
        {
          source->buffer.end = source->buffer.allocation;
          source->buffer.begin = source->buffer.allocation;
        }
    }


  return G_SOURCE_CONTINUE;
}

static void
sysprof_muxer_source_finalize (GSource *gsource)
{
  SysprofMuxerSource *source = (SysprofMuxerSource *)gsource;

  g_clear_fd (&source->capture_fd, NULL);
  g_clear_pointer (&source->writer, sysprof_capture_writer_unref);
  g_clear_pointer (&source->buffer.begin, g_free);
}

static const GSourceFuncs source_funcs = {
  .dispatch = sysprof_muxer_source_dispatch,
  .finalize = sysprof_muxer_source_finalize,
};

GSource *
sysprof_muxer_source_new (int                   capture_fd,
                          SysprofCaptureWriter *writer)
{
  SysprofMuxerSource *source;

  g_return_val_if_fail (capture_fd > -1, NULL);
  g_return_val_if_fail (writer != NULL, NULL);

  source = (SysprofMuxerSource *)g_source_new ((GSourceFuncs *)&source_funcs, sizeof (SysprofMuxerSource));
  source->capture_fd = capture_fd;
  source->writer = sysprof_capture_writer_ref (writer);
  source->buffer.allocation = g_malloc (DEFAULT_BUFFER_SIZE);
  source->buffer.begin = source->buffer.allocation;
  source->buffer.end = source->buffer.allocation;
  source->buffer.capacity = source->buffer.allocation + DEFAULT_BUFFER_SIZE;
  source->buffer.to_skip = sizeof (SysprofCaptureFileHeader);

  g_unix_set_fd_nonblocking (capture_fd, TRUE, NULL);

  g_source_add_unix_fd ((GSource *)source, capture_fd, G_IO_IN);

  return (GSource *)source;
}
