/* mmap-writer.c
 *
 * Copyright 2020 Christian Hergert <chergert@redhat.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * Subject to the terms and conditions of this license, each copyright holder
 * and contributor hereby grants to those receiving rights under this license
 * a perpetual, worldwide, non-exclusive, no-charge, royalty-free,
 * irrevocable (except for failure to satisfy the conditions of this license)
 * patent license to make, have made, use, offer to sell, sell, import, and
 * otherwise transfer this software, where such license applies only to those
 * patent claims, already acquired or hereafter acquired, licensable by such
 * copyright holder or contributor that are necessarily infringed by:
 *
 * (a) their Contribution(s) (the licensed copyrights of copyright holders
 *     and non-copyrightable additions of contributors, in source or binary
 *     form) alone; or
 *
 * (b) combination of their Contribution(s) with the work of authorship to
 *     which such Contribution(s) was added by such copyright holder or
 *     contributor, if, at the time the Contribution is added, such addition
 *     causes such combination to be necessarily infringed. The patent license
 *     shall not apply to any other combinations which include the
 *     Contribution.
 *
 * Except as expressly stated above, no rights or licenses from any copyright
 * holder or contributor is granted under this license, whether expressly, by
 * implication, estoppel or otherwise.
 *
 * DISCLAIMER
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "config.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef HAVE_SYS_MMAN_H
# include <sys/mman.h>
#endif

#include "mmap-writer.h"

#include "sysprof-capture-util-private.h"

struct _MmapWriter
{
  /* The file-descriptor for our underlying file that is mapped
   * into the address space.
   */
  int fd;

  /* The page size we are using for mappings so that we can
   * calculate how many pages to map into address space.
   */
  gsize page_size;

  /* The result of mmap() is stored here so that we can calculate
   * addresses when mmap_writer_advance() is called.
   */
  void *page_map;

  /* Because special information is often stored at the head of a
   * file, we map the head page specially so that it is always available
   * for quick access.
   */
  void *head_page;

  /* The first page that is mapped (the byte offset is calculated
   * from (page*page_size).
   */
  goffset page;

  /* The number of pages that are mapped sequentially. */
  goffset n_pages;

  /* The offset from @page_map for write position. */
  goffset wr_offset;
};

static void
mmap_writer_unmap_head (MmapWriter *self)
{
  g_assert (self != NULL);
  g_assert (self->n_pages > 0);
  g_assert (self->page_size > 0);

  if (self->head_page != NULL)
    {
      msync (self->head_page, self->page_size, MS_SYNC);
      madvise (self->head_page, self->page_size, MADV_DONTNEED);
      munmap (self->head_page, self->page_size);
      self->head_page = NULL;
    }
}

static gboolean
mmap_writer_map_head (MmapWriter *self)
{
  void *map;

  g_assert (self != NULL);
  g_assert (self->head_page == NULL);
  g_assert (self->n_pages > 0);
  g_assert (self->page_size > 0);
  g_assert (self->fd > -1);

  if (posix_fallocate (self->fd, 0, self->page_size) < 0)
    return FALSE;

  map = mmap (NULL, self->page_size, PROT_WRITE | PROT_READ, MAP_SHARED, self->fd, 0);
  if (map == MAP_FAILED)
    return FALSE;

  madvise (map, self->page_size, MADV_WILLNEED);

  self->head_page = map;

  return TRUE;
}

static void
mmap_writer_flush_page_map (MmapWriter *self)
{
  g_assert (self != NULL);

  if (self->page_map != NULL)
    {
      gsize wr_offset = self->wr_offset;
      gsize n_pages = 0;

      while (wr_offset > self->page_size)
        {
          wr_offset -= self->page_size;
          n_pages++;
        }

      if (wr_offset > 0)
        n_pages++;

      msync (self->page_map, n_pages * self->page_size, MS_SYNC);
    }
}

static void
mmap_writer_unmap (MmapWriter *self)
{
  g_assert (self != NULL);
  g_assert (self->n_pages > 0);
  g_assert (self->page_size > 0);

  if (self->page_map != NULL)
    {
      mmap_writer_flush_page_map (self);
      madvise (self->page_map, self->page_size * self->n_pages, MADV_DONTNEED);
      munmap (self->page_map, self->page_size * self->n_pages);
      self->page_map = NULL;
    }
}

static gboolean
mmap_writer_map (MmapWriter *self)
{
  void *map;

  g_assert (self != NULL);
  g_assert (self->page_map == NULL);
  g_assert (self->n_pages > 0);
  g_assert (self->page_size > 0);
  g_assert (self->fd > -1);

  if (posix_fallocate (self->fd,
                       self->page * self->page_size,
                       self->n_pages * self->page_size) < 0)
    return FALSE;

  map = mmap (NULL,
              self->page_size * self->n_pages,
              PROT_WRITE | PROT_READ,
              MAP_SHARED,
              self->fd,
              self->page_size * self->page);
  if (map == MAP_FAILED)
    return FALSE;

  madvise (map,
           self->page_size * self->n_pages,
           (MADV_NOHUGEPAGE | MADV_SEQUENTIAL));

  self->page_map = map;

  return TRUE;
}

static void
normalize_buffer_size (MmapWriter *self,
                       gsize       buffer_size)
{
  g_assert (self != NULL);
  g_assert (self->page_map == NULL);
  g_assert (self->page_size > 0);

  if (buffer_size == 0)
    {
      self->n_pages = 16;
      return;
    }

  self->n_pages = 0;

  while (buffer_size >= self->page_size)
    {
      self->n_pages++;
      buffer_size -= self->page_size;
    }

  if (buffer_size > 0)
    self->n_pages++;

  g_assert (self->n_pages > 0);
}

MmapWriter *
mmap_writer_new_for_fd (gint  fd,
                        gsize buffer_size)
{
  MmapWriter *ret;

  if (fd < 0)
    return NULL;

  ret = g_atomic_rc_box_new0 (MmapWriter);
  ret->fd = fd;
  ret->page_size = _sysprof_getpagesize ();
  ret->page_map = NULL;
  ret->page = 0;
  ret->n_pages = 16;
  ret->wr_offset = sizeof (SysprofCaptureFileHeader);

  normalize_buffer_size (ret, buffer_size);

  if (!mmap_writer_map (ret) || !mmap_writer_map_head (ret))
    {
      close (ret->fd);
      g_atomic_rc_box_release (ret);
    }

  return g_steal_pointer (&ret);
}

MmapWriter *
mmap_writer_new (const gchar *filename,
                 gsize        buffer_size)
{
  gint fd;

  if (filename == NULL)
    return NULL;

  if ((fd = open (filename, O_RDWR | O_CLOEXEC, 0640)) == -1)
    return NULL;

  return mmap_writer_new_for_fd (fd, buffer_size);
}

void
mmap_writer_close (MmapWriter *self)
{
  g_assert (self != NULL);

  mmap_writer_unmap (self);
  mmap_writer_unmap_head (self);

  if (self->fd > -1)
    {
      close (self->fd);
      self->fd = -1;
    }

  self->wr_offset = 0;
  self->page = 0;
  self->n_pages = 0;

  g_assert (self->fd == -1);
  g_assert (self->head_page == NULL);
  g_assert (self->page_map == NULL);
}

void
mmap_writer_destroy (MmapWriter *self)
{
  g_atomic_rc_box_release_full (self, (GDestroyNotify)mmap_writer_close);
}

gint
mmap_writer_get_fd (MmapWriter *self)
{
  return self != NULL ? self->fd : -1;
}

static inline gboolean
mmap_writer_has_space_for (MmapWriter *self,
                           goffset     length)
{
  goffset available;

  g_assert (self != NULL);
  g_assert (self->fd > -1);
  g_assert (self->page_map != NULL);
  g_assert (self->page_size > 0);
  g_assert (self->n_pages > 0);

  available = (self->page_size * self->n_pages) - self->wr_offset;

  return length < available;
}

gpointer
mmap_writer_advance (MmapWriter *self,
                     goffset     length)
{
  void *ret;

  g_assert (self != NULL);
  g_assert (self->fd > -1);
  g_assert (self->page_map != NULL);
  g_assert (self->page_size > 0);
  g_assert (self->n_pages > 0);
  g_assert (self->wr_offset <= (self->n_pages * self->page_size));

  if G_UNLIKELY (!mmap_writer_has_space_for (self, length))
    {
      goffset req_pages;

      mmap_writer_unmap (self);

      while (self->wr_offset > self->page_size)
        {
          self->page++;
          self->wr_offset -= self->page_size;
        }

      /* Determine how many pages we need loaded */
      req_pages = (self->wr_offset + length) / self->page_size;
      if (((self->wr_offset + length) % self->page_size) > 0)
        req_pages++;

      /* We might need to increase the buffer size */
      if (req_pages > self->n_pages)
        self->n_pages = req_pages;

      if (!mmap_writer_map (self))
        return NULL;
    }

  /* Stash pointer for the frame we've just added */
  ret = (guint8 *)self->page_map + self->wr_offset;

  /* Now advance our write offset */
  self->wr_offset += length;

  return ret;
}

gpointer
mmap_writer_rewind (MmapWriter *self,
                    goffset     length)
{
  g_assert (self != NULL);
  g_assert (self->fd > -1);
  g_assert (self->page_map != NULL);
  g_assert (self->page_size > 0);
  g_assert (self->n_pages > 0);

  /* We only allow rewinding into the current frame, but we can short-
   * circuit and technically allow it within the current map range so
   * we don't have to track the current frame size.
   */
  if (length > self->wr_offset)
    return NULL;

  self->wr_offset -= length;

  return (guint8 *)self->page_map + self->wr_offset;
}

void
mmap_writer_flush (MmapWriter *self)
{
  g_return_if_fail (self != NULL);

  if (self->head_page != NULL)
    msync (self->head_page, self->page_size, MS_SYNC);

  if (self->page_map != NULL)
    mmap_writer_flush_page_map (self);
}

SysprofCaptureFileHeader *
mmap_writer_get_file_header (MmapWriter *self)
{
  g_return_val_if_fail (self != NULL, GSIZE_TO_POINTER (-1));
  g_return_val_if_fail (self->head_page != NULL, GSIZE_TO_POINTER (-1));

  return (SysprofCaptureFileHeader *)self->head_page;
}

gsize
mmap_writer_get_buffer_size (MmapWriter *self)
{
  g_return_val_if_fail (self != NULL, 0);

  return self->n_pages * self->page_size;
}

goffset
mmap_writer_tell (MmapWriter *self)
{
  g_return_val_if_fail (self->head_page != NULL, -1);
  g_return_val_if_fail (self->page_map != NULL, -1);

  return self->page * self->page_size + self->wr_offset;
}
