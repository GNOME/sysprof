/* mapped-ring-buffer.c
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

#define G_LOG_DOMAIN "mapped-ring-buffer"

#include "config.h"

#include <stdatomic.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include "sysprof-capture-util-private.h"
#include "sysprof-platform.h"

#include "mapped-ring-buffer.h"

#define DEFAULT_N_PAGES 32
#define BUFFER_MAX_SIZE ((G_MAXUINT32/2)-_sysprof_getpagesize())

enum {
  MODE_READER = 1,
  MODE_WRITER = 2,
};

/*
 * MappedRingFrame is the header on each buffer entry so that
 * we can stay 8-byte aligned.
 */
typedef struct _MappedRingFrame
{
  guint64 len : 32;
  guint64 padding : 32;
} MappedRingFrame;

G_STATIC_ASSERT (sizeof (MappedRingFrame) == 8);

/*
 * MappedRingHeader is the header of the first page of the
 * buffer. We use the whole buffer so that we can double map
 * the body of the buffer.
 */
typedef struct _MappedRingHeader
{
  guint32 head;
  guint32 tail;
  guint32 offset;
  guint32 size;
} MappedRingHeader;

G_STATIC_ASSERT (sizeof (MappedRingHeader) == 16);

/*
 * MappedRingBuffer is used to wrap both the reader and writer
 * mapping structures.
 */
struct _MappedRingBuffer
{
  int    mode;
  int    fd;
  void  *map;
  gsize  body_size;
  gsize  page_size;
};

static inline MappedRingHeader *
get_header (MappedRingBuffer *self)
{
  return (MappedRingHeader *)self->map;
}

static inline gpointer
get_body_at_pos (MappedRingBuffer *self,
                 gsize             pos)
{
  g_assert (pos < (self->body_size + self->body_size - sizeof (MappedRingFrame)));

  return (guint8 *)self->map + self->page_size + pos;
}

static gpointer
map_head_and_body_twice (int   fd,
                         gsize head_size,
                         gsize body_size)
{
  void *map;
  void *second;

  /* First we map FD to the total size we want so that we can be
   * certain the OS will give us a contiguous mapping for our buffers
   * even though we can't dereference a portion of the mapping yet.
   *
   * We'll overwrite the secondary mapping in a moment to deal with
   * wraparound for the ring buffer.
   */
  map = mmap (NULL,
              head_size + body_size + body_size,
              PROT_READ | PROT_WRITE,
              MAP_SHARED,
              fd,
              0);

  if (map == MAP_FAILED)
    return NULL;

  /* At this point, we have [HEAD|BODY|BUSERR] mapped. But we want to map
   * the body again over what would currently cause a BusError. That way
   * when we need to wraparound we don't need to copy anything, we just
   * have to check in mapped_ring_buffer_allocate() that the size does not
   * step over what would be the real reader position.
   *
   * By mmap()'ing over the old region, the previous region is automatically
   * munmap()'d for us.
   */
  second = mmap ((guint8 *)map + head_size + body_size,
                 body_size,
                 PROT_READ | PROT_WRITE,
                 MAP_SHARED | MAP_FIXED,
                 fd,
                 head_size);

  if (second == MAP_FAILED)
    {
      munmap (map, head_size + body_size + body_size);
      return NULL;
    }

  g_assert (second == (gpointer)((guint8 *)map + head_size + body_size));

  return map;
}

/**
 * mapped_ring_buffer_new_reader:
 * @buffer_size: the size of the buffer, which must be page-aligned
 *
 * Creates a new #MappedRingBuffer.
 *
 * This should be called by the process reading the buffer. It should
 * then pass the FD of the buffer to another process or thread to
 * advance the ring buffer by writing data.
 *
 * The other process or thread should create a new #MappedRingBuffer
 * using mapped_ring_buffer_new_writer() with the FD retrieved from
 * the reader using mapped_ring_buffer_get_fd(). If crossing a process
 * boundary, you probably also want to dup() the FD and set O_CLOEXEC.
 *
 * @buffer_size must be a multiple of the system's page size which can
 * be retrieved using sysprof_getpagesize().
 *
 * Returns: (transfer full): a #MappedRingBuffer
 */
MappedRingBuffer *
mapped_ring_buffer_new_reader (gsize buffer_size)
{
  MappedRingBuffer *self;
  MappedRingHeader *header;
  gsize page_size;
  void *map;
  int fd;

  g_return_val_if_fail ((buffer_size % _sysprof_getpagesize ()) == 0, NULL);
  g_return_val_if_fail (buffer_size < BUFFER_MAX_SIZE, NULL);

  page_size = _sysprof_getpagesize ();

  /* Add 1 page for coordination header */
  if (buffer_size == 0)
    buffer_size = page_size * DEFAULT_N_PAGES;
  buffer_size += page_size;

  /* Create our memfd (or tmpfs) for writing */
  if ((fd = sysprof_memfd_create ("[sysprof-ring-buffer]")) == -1)
    return NULL;

  /* Size our memfd to reserve space */
  if (ftruncate (fd, buffer_size) != 0)
    {
      close (fd);
      return NULL;
    }

  /* Map ring buffer [HEAD|BODY|BODY] */
  if (!(map = map_head_and_body_twice (fd, page_size, buffer_size - page_size)))
    {
      close (fd);
      return NULL;
    }

  /* Setup initial header */
  header = map;
  header->head = 0;
  header->tail = 0;
  header->offset = page_size;
  header->size = buffer_size - page_size;

  self = g_atomic_rc_box_new0 (MappedRingBuffer);
  self->mode = MODE_READER;
  self->body_size = buffer_size - page_size;
  self->fd = fd;
  self->map = map;
  self->page_size = page_size;

  return g_steal_pointer (&self);
}

/**
 * mapped_ring_buffer_new_writer:
 * @fd: a FD to map
 *
 * Creates a new #MappedRingBuffer using a copy of @fd.
 *
 * The caller may close(fd) after calling this function regardless of
 * success creating the #MappedRingBuffer.
 *
 * Returns: (transfer full) (nullable): a new #MappedRingBuffer
 */
MappedRingBuffer *
mapped_ring_buffer_new_writer (gint fd)
{
  MappedRingBuffer *self;
  MappedRingHeader *header;
  gssize buffer_size;
  gsize page_size;
  void *map;

  g_return_val_if_fail (fd > -1, NULL);

  page_size = _sysprof_getpagesize ();

  /* Make our own copy of the FD */
  if ((fd = dup (fd)) < 0)
    {
      g_printerr ("Failed to dup() fd, cannot continue\n");
      return NULL;
    }

  /* Seek to end to get buffer size */
  if ((buffer_size = lseek (fd, 0, SEEK_END)) < 0)
    {
      g_printerr ("Failed to seek to end of file. Cannot determine buffer size.\n");
      return NULL;
    }

  /* Ensure non-zero sized buffer */
  if (buffer_size < (page_size + page_size))
    {
      g_printerr ("Buffer is too small, cannot continue.\n");
      return NULL;
    }

  /* Make sure it is less than our max size */
  if ((buffer_size - page_size) > BUFFER_MAX_SIZE)
    {
      g_printerr ("Buffer is too large, cannot continue.\n");
      return NULL;
    }

  /* Ensure we have page-aligned buffer */
  if ((buffer_size % page_size) != 0)
    {
      g_printerr ("Invalid buffer size, not page aligned.\n");
      return NULL;
    }

  /* Map ring buffer [HEAD|BODY|BODY] */
  if (!(map = map_head_and_body_twice (fd, page_size, buffer_size - page_size)))
    {
      close (fd);
      return NULL;
    }

  /* Validate we got proper data in header */
  header = map;
  if (header->offset != page_size ||
      header->size != (buffer_size - page_size))
    {
      munmap (map, page_size + ((buffer_size - page_size) * 2));
      close (fd);
      return NULL;
    }

  self = g_atomic_rc_box_new0 (MappedRingBuffer);
  self->mode = MODE_WRITER;
  self->fd = fd;
  self->body_size = buffer_size - page_size;
  self->map = map;
  self->page_size = page_size;

  return g_steal_pointer (&self);
}

static void
mapped_ring_buffer_finalize (MappedRingBuffer *self)
{
  if (self->map != NULL)
    {
      munmap (self->map, self->page_size + self->body_size + self->body_size);
      self->map = NULL;
    }

  if (self->fd != -1)
    {
      close (self->fd);
      self->fd = -1;
    }
}

void
mapped_ring_buffer_unref (MappedRingBuffer *self)
{
  g_atomic_rc_box_release_full (self, (GDestroyNotify)mapped_ring_buffer_finalize);
}

MappedRingBuffer *
mapped_ring_buffer_ref (MappedRingBuffer *self)
{
  return g_atomic_rc_box_acquire (self);
}

gint
mapped_ring_buffer_get_fd (MappedRingBuffer *self)
{
  g_return_val_if_fail (self != NULL, -1);

  return self->fd;
}

/**
 * mapped_ring_buffer_allocate:
 * @self: a #MappedRingBuffer
 *
 * Ensures that @length bytes are available at the next position in
 * the ring buffer and returns a pointer to the beginning of that zone.
 *
 * If the reader has not read enough bytes to allow @length to be added
 * then a mark will be added or incremented notifying the peer of how
 * many records they have lost and %NULL is returned.
 *
 * You must always check for %NULL before dereferencing the result of
 * this function as space may not be immediately available.
 *
 * This only ensure that the space is available for you to write. To
 * notify the peer that the zone is ready for reading you must call
 * mapped_ring_buffer_advance() with the number of bytes to advance.
 * This is useful in case you need to allocate more memory than you
 * might need up-front but commit a smaller amount.
 *
 * Returns: (nullable): a pointer to data of at least @length bytes
 *   or %NULL if there is not enough space.
 */
gpointer
mapped_ring_buffer_allocate (MappedRingBuffer *self,
                             gsize             length)
{
  MappedRingHeader *header;
  gsize headpos;
  gsize tailpos;

  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (self->mode == MODE_WRITER, NULL);
  g_return_val_if_fail (length > 0, NULL);
  g_return_val_if_fail (length < self->body_size, NULL);
  g_return_val_if_fail ((length & 0x7) == 0, NULL);

  length += sizeof (MappedRingFrame);

  header = get_header (self);
  headpos = g_atomic_int_get (&header->head);
  tailpos = g_atomic_int_get (&header->tail);

  /* We need to check that there is enough space for @length at the
   * current position in the write buffer. We cannot fully catch up
   * to head, we must be at least one byte short of it. If we do not
   * have enough space, then return NULL.
   *
   * When we have finished writing our frame data, we will push the tail
   * forward with an atomic write.
   */

  if (tailpos == headpos)
    return get_body_at_pos (self, tailpos + sizeof (MappedRingFrame));

  if (headpos < tailpos)
    headpos += self->body_size;

  if (tailpos + length < headpos)
    return get_body_at_pos (self, tailpos + sizeof (MappedRingFrame));

  return NULL;
}

/**
 * mapped_ring_buffer_advance:
 * @self: a #MappedRingBuffer
 * @length: a 8-byte aligned length
 *
 * Advances the ring buffer @length bytes forward. @length must be
 * 8-byte aligned so that the buffer may avoid memcpy() to read
 * framing data.
 *
 * This should only be called by a writer created with
 * mapped_ring_buffer_new_writer().
 *
 * Call this after writing your data into the buffer using
 * mapped_ring_buffer_allocate().
 *
 * It is a programming error to call this with a value greater
 * than was called to mapped_ring_buffer_allocate().
 */
void
mapped_ring_buffer_advance (MappedRingBuffer *self,
                            gsize             length)
{
  MappedRingHeader *header;
  MappedRingFrame *fr;
  guint32 tail;

  g_return_if_fail (self != NULL);
  g_return_if_fail (self->mode == MODE_WRITER);
  g_return_if_fail (length > 0);
  g_return_if_fail (length < self->body_size);
  g_return_if_fail ((length & 0x7) == 0);

  header = get_header (self);
  tail = header->tail;

  /* First write the frame header with the data length */
  fr = get_body_at_pos (self, tail);
  fr->len = length;
  fr->padding = 0;

  /* Now calculate the new tail position */
  tail = tail + sizeof *fr + length;
  if (tail >= self->body_size)
    tail -= self->body_size;

  /* We have already checked that we could advance the buffer when the
   * application called mapped_ring_buffer_allocate(), so at this point
   * we just update the position as the only way the head could have
   * moved is forward.
   */
  g_atomic_int_set (&header->tail, tail);
}

/**
 * mapped_ring_buffer_drain:
 * @self: a #MappedRingBuffer
 * @callback: (scope call): a callback to execute for each frame
 * @user_data: closure data for @callback
 *
 * Drains the buffer by calling @callback for each frame.
 *
 * This should only be called by a reader created with
 * mapped_ring_buffer_new_reader().
 *
 * Returns: %TRUE if the buffer was drained, %FALSE if @callback prematurely
 *   returned while draining.
 */
gboolean
mapped_ring_buffer_drain (MappedRingBuffer         *self,
                          MappedRingBufferCallback  callback,
                          gpointer                  user_data)
{
  MappedRingHeader *header;
  gsize headpos;
  gsize tailpos;
  gboolean ret = FALSE;

  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (self->mode == MODE_READER, FALSE);
  g_return_val_if_fail (callback != NULL, FALSE);

  header = get_header (self);
  headpos = g_atomic_int_get (&header->head);
  tailpos = g_atomic_int_get (&header->tail);

  g_assert (headpos < self->body_size);
  g_assert (tailpos < self->body_size);

  if (headpos == tailpos)
    return TRUE;

  /* If head needs to wrap around to get to tail, we can just rely on
   * our double mapping instead actually manually wrapping/copying data.
   */
  if (tailpos < headpos)
    tailpos += self->body_size;

  g_assert (headpos < tailpos);

  while (headpos < tailpos)
    {
      const MappedRingFrame *fr = get_body_at_pos (self, headpos);
      gconstpointer data = (guint8 *)fr + sizeof *fr;

      headpos = headpos + sizeof *fr + fr->len;

      if (!callback (data, fr->len, user_data))
        goto short_circuit;
    }

  ret = TRUE;

short_circuit:

  if (headpos >= self->body_size)
    headpos -= self->body_size;

  g_assert (headpos < self->body_size);

  g_atomic_int_set (&header->head, headpos);

  return ret;
}

typedef struct _MappedRingSource
{
  GSource           source;
  MappedRingBuffer *self;
} MappedRingSource;

static gboolean
mapped_ring_source_dispatch (GSource     *source,
                             GSourceFunc  callback,
                             gpointer     user_data)
{
  MappedRingSource *real_source = (MappedRingSource *)source;

  g_assert (source != NULL);

  return mapped_ring_buffer_drain (real_source->self,
                                   (MappedRingBufferCallback)callback,
                                   user_data);
}

static void
mapped_ring_source_finalize (GSource *source)
{
  MappedRingSource *real_source = (MappedRingSource *)source;

  if (real_source != NULL)
    g_clear_pointer (&real_source->self, mapped_ring_buffer_unref);
}

static gboolean
mapped_ring_source_check (GSource *source)
{
  MappedRingSource *real_source = (MappedRingSource *)source;
  MappedRingHeader *header;

  g_assert (real_source != NULL);
  g_assert (real_source->self != NULL);

  header = get_header (real_source->self);

  if (g_atomic_int_get (&header->head) != g_atomic_int_get (&header->tail))
    return TRUE;

  return FALSE;
}

static gboolean
mapped_ring_source_prepare (GSource *source,
                            gint    *timeout_)
{
  MappedRingSource *real_source = (MappedRingSource *)source;
  MappedRingHeader *header;

  g_assert (real_source != NULL);
  g_assert (real_source->self != NULL);

  header = get_header (real_source->self);

  if (g_atomic_int_get (&header->head) != g_atomic_int_get (&header->tail))
    return TRUE;

  *timeout_ = 1000 / 20; /* 20x a second */

  return FALSE;
}

static GSourceFuncs mapped_ring_source_funcs = {
  .prepare  = mapped_ring_source_prepare,
  .check    = mapped_ring_source_check,
  .dispatch = mapped_ring_source_dispatch,
  .finalize = mapped_ring_source_finalize,
};

guint
mapped_ring_buffer_create_source (MappedRingBuffer         *self,
                                  MappedRingBufferCallback  source_func,
                                  gpointer                  user_data)
{
  MappedRingSource *source;

  g_return_val_if_fail (self != NULL, 0);
  g_return_val_if_fail (source_func != NULL, 0);

  /* TODO: Can we use G_IO_IN with the memfd? */

  source = (MappedRingSource *)g_source_new (&mapped_ring_source_funcs, sizeof (MappedRingSource));
  source->self = mapped_ring_buffer_ref (self);
  g_source_set_callback ((GSource *)source, (GSourceFunc)source_func, user_data, NULL);
  g_source_set_name ((GSource *)source, "MappedRingSource");

  return g_source_attach ((GSource *)source, g_main_context_default ());
}
