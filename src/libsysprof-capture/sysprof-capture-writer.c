/* sysprof-capture-writer.c
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sysprof-capture-writer"

#include "config.h"

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "mmap-writer.h"

#include "sysprof-capture-reader.h"
#include "sysprof-capture-util-private.h"
#include "sysprof-capture-writer.h"

#define DEFAULT_BUFFER_SIZE (_sysprof_getpagesize() * 64L)
#define INVALID_ADDRESS     (G_GUINT64_CONSTANT(0))
#define MAX_COUNTERS        ((1 << 24) - 1)
#define MAX_UNWIND_DEPTH    128

typedef struct
{
  /* A pinter into the string buffer */
  const gchar *str;

  /* The unique address for the string */
  guint64 addr;
} SysprofCaptureJitmapBucket;

struct _SysprofCaptureWriter
{
  /* Our hashtable for JIT deduplication.
   *
   * Our address sequence counter is the value that comes from
   * monotonically increasing this is OR'd with JITMAP_MARK to denote
   * the address name should come from the JIT map.
   *
   * The addr_hash_size is the number of items in @addr_hash. This is
   * an optimization so that we avoid calculating the number of strings
   * when flushing out the jitmap.
   */
  guint8 addr_buf[3584];
  SysprofCaptureJitmapBucket addr_hash[512];
  gsize addr_buf_pos;
  gsize addr_seq;
  guint addr_hash_size;

  /* The MmapWriter abstracts writing to a memory mapped file which is
   * persisted to underlying storage. The goal here is to ensure that as
   * we write data, it can be retrieved in case our program crashes. This
   * is particularly useful when the external Sysprof profiler stops the
   * profiler but the inferior process does not exit. The profiler can
   * read up to the known data and then skip any unfinished data when
   * muxing the streams.
   *
   * To make updating the SysprofCaptureFileHeader faster the MmapWriter
   * keeps the head page around and available.
   */
  MmapWriter *writer;

  /* GSource for periodic flush */
  GSource *periodic_flush;

  /* counter id sequence */
  gint next_counter_id;

  /* Statistics while recording */
  SysprofCaptureStat stat;
};

static inline void
sysprof_capture_writer_frame_init (SysprofCaptureFrame     *frame_,
                                   gint                     len,
                                   gint                     cpu,
                                   gint32                   pid,
                                   gint64                   time_,
                                   SysprofCaptureFrameType  type)
{
  frame_->len = len;
  frame_->cpu = cpu;
  frame_->pid = pid;
  frame_->time = time_;
  frame_->type = type;
  frame_->padding1 = 0;
  frame_->padding2 = 0;
}

static void
sysprof_capture_writer_finalize (SysprofCaptureWriter *self)
{
  if (self != NULL)
    {
      g_clear_pointer (&self->periodic_flush, g_source_destroy);
      sysprof_capture_writer_flush (self);
      g_clear_pointer (&self->writer, mmap_writer_destroy);
    }
}

SysprofCaptureWriter *
sysprof_capture_writer_ref (SysprofCaptureWriter *self)
{
  return g_atomic_rc_box_acquire (self);
}

void
sysprof_capture_writer_unref (SysprofCaptureWriter *self)
{
  g_atomic_rc_box_release_full (self, (GDestroyNotify)sysprof_capture_writer_finalize);
}

static gboolean
sysprof_capture_writer_flush_data (SysprofCaptureWriter *self)
{
  g_assert (self != NULL);

  if (self->writer != NULL)
    mmap_writer_flush (self->writer);

  return TRUE;
}

static inline void
sysprof_capture_writer_realign (gsize *pos)
{
  *pos = (*pos + SYSPROF_CAPTURE_ALIGN - 1) & ~(SYSPROF_CAPTURE_ALIGN - 1);
}

static inline gpointer
sysprof_capture_writer_allocate (SysprofCaptureWriter *self,
                                 gsize                *len)
{
  g_assert (self != NULL);
  g_assert (len != NULL);
  g_assert (self->writer != NULL);

  sysprof_capture_writer_realign (len);

  return mmap_writer_advance (self->writer, *len);
}

static gboolean
sysprof_capture_writer_flush_jitmap (SysprofCaptureWriter *self)
{
  SysprofCaptureJitmap *jitmap;
  gsize len;

  g_assert (self != NULL);

  if (self->addr_hash_size == 0)
    return TRUE;

  g_assert (self->addr_buf_pos > 0);

  len = sizeof *jitmap + self->addr_buf_pos;
  sysprof_capture_writer_realign (&len);

  jitmap = mmap_writer_advance (self->writer, len);
  sysprof_capture_writer_frame_init (&jitmap->frame,
                                     len,
                                     -1,
                                     _sysprof_getpid (),
                                     SYSPROF_CAPTURE_CURRENT_TIME,
                                     SYSPROF_CAPTURE_FRAME_JITMAP);
  jitmap->n_jitmaps = self->addr_hash_size;

  /* Copy the jitmap buffer into the frame data */
  memcpy (jitmap->data, self->addr_buf, self->addr_buf_pos);

  self->addr_buf_pos = 0;
  self->addr_hash_size = 0;
  memset (self->addr_hash, 0, sizeof self->addr_hash);

  self->stat.frame_count[SYSPROF_CAPTURE_FRAME_JITMAP]++;

  return TRUE;
}

static gboolean
sysprof_capture_writer_lookup_jitmap (SysprofCaptureWriter  *self,
                                      const gchar           *name,
                                      SysprofCaptureAddress *addr)
{
  guint hash;
  guint i;

  g_assert (self != NULL);
  g_assert (name != NULL);
  g_assert (addr != NULL);

  hash = g_str_hash (name) % G_N_ELEMENTS (self->addr_hash);

  for (i = hash; i < G_N_ELEMENTS (self->addr_hash); i++)
    {
      SysprofCaptureJitmapBucket *bucket = &self->addr_hash[i];

      if (bucket->str == NULL)
        return FALSE;

      if (strcmp (bucket->str, name) == 0)
        {
          *addr = bucket->addr;
          return TRUE;
        }
    }

  for (i = 0; i < hash; i++)
    {
      SysprofCaptureJitmapBucket *bucket = &self->addr_hash[i];

      if (bucket->str == NULL)
        return FALSE;

      if (strcmp (bucket->str, name) == 0)
        {
          *addr = bucket->addr;
          return TRUE;
        }
    }

  return FALSE;
}

static SysprofCaptureAddress
sysprof_capture_writer_insert_jitmap (SysprofCaptureWriter *self,
                                      const gchar          *str)
{
  SysprofCaptureAddress addr;
  gchar *dst;
  gsize len;
  guint hash;
  guint i;

  g_assert (self != NULL);
  g_assert (str != NULL);

  len = sizeof addr + strlen (str) + 1;

  if ((self->addr_hash_size == G_N_ELEMENTS (self->addr_hash)) ||
      ((sizeof self->addr_buf - self->addr_buf_pos) < len))
    {
      if (!sysprof_capture_writer_flush_jitmap (self))
        return INVALID_ADDRESS;

      g_assert (self->addr_hash_size == 0);
      g_assert (self->addr_buf_pos == 0);
    }

  g_assert (self->addr_hash_size < G_N_ELEMENTS (self->addr_hash));
  g_assert (len > sizeof addr);

  /* Allocate the next unique address */
  addr = SYSPROF_CAPTURE_JITMAP_MARK | ++self->addr_seq;

  /* Copy the address into the buffer */
  dst = (gchar *)&self->addr_buf[self->addr_buf_pos];
  memcpy (dst, &addr, sizeof addr);

  /*
   * Copy the string into the buffer, keeping dst around for
   * when we insert into the hashtable.
   */
  dst += sizeof addr;
  memcpy (dst, str, len - sizeof addr);

  /* Advance our string cache position */
  self->addr_buf_pos += len;
  g_assert (self->addr_buf_pos <= sizeof self->addr_buf);

  /* Now place the address into the hashtable */
  hash = g_str_hash (str) % G_N_ELEMENTS (self->addr_hash);

  /* Start from the current hash bucket and go forward */
  for (i = hash; i < G_N_ELEMENTS (self->addr_hash); i++)
    {
      SysprofCaptureJitmapBucket *bucket = &self->addr_hash[i];

      if (G_LIKELY (bucket->str == NULL))
        {
          bucket->str = dst;
          bucket->addr = addr;
          self->addr_hash_size++;
          return addr;
        }
    }

  /* Wrap around to the beginning */
  for (i = 0; i < hash; i++)
    {
      SysprofCaptureJitmapBucket *bucket = &self->addr_hash[i];

      if (G_LIKELY (bucket->str == NULL))
        {
          bucket->str = dst;
          bucket->addr = addr;
          self->addr_hash_size++;
          return addr;
        }
    }

  g_assert_not_reached ();

  return INVALID_ADDRESS;
}

SysprofCaptureWriter *
sysprof_capture_writer_new_from_fd (int   fd,
                                    gsize buffer_size)
{
  g_autofree gchar *nowstr = NULL;
  g_autoptr(GDateTime) now = NULL;
  SysprofCaptureWriter *self;
  SysprofCaptureFileHeader *header;

  if (fd < 0)
    return NULL;

  if (buffer_size == 0)
    buffer_size = DEFAULT_BUFFER_SIZE;

  g_assert (fd != -1);
  g_assert (buffer_size % _sysprof_getpagesize() == 0);

  /* This is only useful on files, memfd, etc */
  if (ftruncate (fd, 0) != 0) { /* Do Nothing */ }

  self = g_atomic_rc_box_new0 (SysprofCaptureWriter);
  self->writer = mmap_writer_new_for_fd (fd, buffer_size);
  self->next_counter_id = 1;

  now = g_date_time_new_now_local ();
  nowstr = g_date_time_format_iso8601 (now);

  if (!(header = mmap_writer_get_file_header (self->writer)))
    {
      sysprof_capture_writer_finalize (self);
      return NULL;
    }

  header->magic = SYSPROF_CAPTURE_MAGIC;
  header->version = 1;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  header->little_endian = TRUE;
#else
  header->little_endian = FALSE;
#endif
  header->padding = 0;
  g_strlcpy (header->capture_time, nowstr, sizeof header->capture_time);
  header->time = SYSPROF_CAPTURE_CURRENT_TIME;
  header->end_time = 0;
  memset (header->suffix, 0, sizeof header->suffix);

  mmap_writer_flush (self->writer);

  return self;
}

SysprofCaptureWriter *
sysprof_capture_writer_new (const gchar *filename,
                            gsize        buffer_size)
{
  SysprofCaptureWriter *self;
  int fd;

  g_assert (filename != NULL);
  g_assert (buffer_size % _sysprof_getpagesize() == 0);

  if ((-1 == (fd = open (filename, O_CREAT | O_RDWR, 0640))) ||
      (-1 == ftruncate (fd, 0L)))
    return NULL;

  self = sysprof_capture_writer_new_from_fd (fd, buffer_size);

  if (self == NULL)
    close (fd);

  return self;
}

gboolean
sysprof_capture_writer_add_map (SysprofCaptureWriter *self,
                                gint64                time,
                                gint                  cpu,
                                gint32                pid,
                                guint64               start,
                                guint64               end,
                                guint64               offset,
                                guint64               inode,
                                const gchar          *filename)
{
  SysprofCaptureMap *ev;
  gsize len;

  if (filename == NULL)
    filename = "";

  g_assert (self != NULL);
  g_assert (filename != NULL);

  len = sizeof *ev + strlen (filename) + 1;
  if (!(ev = sysprof_capture_writer_allocate (self, &len)))
    return FALSE;

  sysprof_capture_writer_frame_init (&ev->frame,
                                     len,
                                     cpu,
                                     pid,
                                     time,
                                     SYSPROF_CAPTURE_FRAME_MAP);
  ev->start = start;
  ev->end = end;
  ev->offset = offset;
  ev->inode = inode;

  g_strlcpy (ev->filename, filename, len - sizeof *ev);
  ev->filename[len - sizeof *ev - 1] = '\0';

  self->stat.frame_count[SYSPROF_CAPTURE_FRAME_MAP]++;

  return TRUE;
}

gboolean
sysprof_capture_writer_add_mark (SysprofCaptureWriter *self,
                                 gint64                time,
                                 gint                  cpu,
                                 gint32                pid,
                                 guint64               duration,
                                 const gchar          *group,
                                 const gchar          *name,
                                 const gchar          *message)
{
  SysprofCaptureMark *ev;
  gsize message_len;
  gsize len;

  g_assert (self != NULL);
  g_assert (name != NULL);
  g_assert (group != NULL);

  if (message == NULL)
    message = "";
  message_len = strlen (message) + 1;

  len = sizeof *ev + message_len;
  if (!(ev = sysprof_capture_writer_allocate (self, &len)))
    return FALSE;

  sysprof_capture_writer_frame_init (&ev->frame,
                                     len,
                                     cpu,
                                     pid,
                                     time,
                                     SYSPROF_CAPTURE_FRAME_MARK);

  ev->duration = duration;
  g_strlcpy (ev->group, group, sizeof ev->group);
  g_strlcpy (ev->name, name, sizeof ev->name);
  memcpy (ev->message, message, message_len);

  self->stat.frame_count[SYSPROF_CAPTURE_FRAME_MARK]++;

  return TRUE;
}

gboolean
sysprof_capture_writer_add_metadata (SysprofCaptureWriter *self,
                                     gint64                time,
                                     gint                  cpu,
                                     gint32                pid,
                                     const gchar          *id,
                                     const gchar          *metadata,
                                     gssize                metadata_len)
{
  SysprofCaptureMetadata *ev;
  gsize len;

  g_assert (self != NULL);
  g_assert (id != NULL);

  if (metadata == NULL)
    {
      metadata = "";
      len = 0;
    }

  if (metadata_len < 0)
    metadata_len = strlen (metadata);

  len = sizeof *ev + metadata_len + 1;
  if (!(ev = sysprof_capture_writer_allocate (self, &len)))
    return FALSE;

  sysprof_capture_writer_frame_init (&ev->frame,
                                     len,
                                     cpu,
                                     pid,
                                     time,
                                     SYSPROF_CAPTURE_FRAME_METADATA);

  g_strlcpy (ev->id, id, sizeof ev->id);
  memcpy (ev->metadata, metadata, metadata_len);
  ev->metadata[metadata_len] = 0;

  self->stat.frame_count[SYSPROF_CAPTURE_FRAME_METADATA]++;

  return TRUE;
}

SysprofCaptureAddress
sysprof_capture_writer_add_jitmap (SysprofCaptureWriter *self,
                                   const gchar          *name)
{
  SysprofCaptureAddress addr = INVALID_ADDRESS;

  if (name == NULL)
    name = "";

  g_assert (self != NULL);
  g_assert (name != NULL);

  if (!sysprof_capture_writer_lookup_jitmap (self, name, &addr))
    addr = sysprof_capture_writer_insert_jitmap (self, name);

  return addr;
}

gboolean
sysprof_capture_writer_add_process (SysprofCaptureWriter *self,
                                    gint64                time,
                                    gint                  cpu,
                                    gint32                pid,
                                    const gchar          *cmdline)
{
  SysprofCaptureProcess *ev;
  gsize len;

  if (cmdline == NULL)
    cmdline = "";

  g_assert (self != NULL);
  g_assert (cmdline != NULL);

  len = sizeof *ev + strlen (cmdline) + 1;
  if (!(ev = sysprof_capture_writer_allocate (self, &len)))
    return FALSE;

  sysprof_capture_writer_frame_init (&ev->frame,
                                     len,
                                     cpu,
                                     pid,
                                     time,
                                     SYSPROF_CAPTURE_FRAME_PROCESS);

  g_strlcpy (ev->cmdline, cmdline, len - sizeof *ev);
  ev->cmdline[len - sizeof *ev - 1] = '\0';

  self->stat.frame_count[SYSPROF_CAPTURE_FRAME_PROCESS]++;

  return TRUE;
}

gboolean
sysprof_capture_writer_add_sample (SysprofCaptureWriter        *self,
                                   gint64                       time,
                                   gint                         cpu,
                                   gint32                       pid,
                                   gint32                       tid,
                                   const SysprofCaptureAddress *addrs,
                                   guint                        n_addrs)
{
  SysprofCaptureSample *ev;
  gsize len;

  g_assert (self != NULL);

  len = sizeof *ev + (n_addrs * sizeof (SysprofCaptureAddress));
  if (!(ev = sysprof_capture_writer_allocate (self, &len)))
    return FALSE;

  sysprof_capture_writer_frame_init (&ev->frame,
                                     len,
                                     cpu,
                                     pid,
                                     time,
                                     SYSPROF_CAPTURE_FRAME_SAMPLE);
  ev->n_addrs = n_addrs;
  ev->tid = tid;

  memcpy (ev->addrs, addrs, (n_addrs * sizeof (SysprofCaptureAddress)));

  self->stat.frame_count[SYSPROF_CAPTURE_FRAME_SAMPLE]++;

  return TRUE;
}

gboolean
sysprof_capture_writer_add_fork (SysprofCaptureWriter *self,
                                 gint64           time,
                                 gint             cpu,
                                 gint32           pid,
                                 gint32           child_pid)
{
  SysprofCaptureFork *ev;
  gsize len;

  g_assert (self != NULL);

  len = sizeof *ev;
  if (!(ev = sysprof_capture_writer_allocate (self, &len)))
    return FALSE;

  sysprof_capture_writer_frame_init (&ev->frame,
                                     len,
                                     cpu,
                                     pid,
                                     time,
                                     SYSPROF_CAPTURE_FRAME_FORK);
  ev->child_pid = child_pid;

  self->stat.frame_count[SYSPROF_CAPTURE_FRAME_FORK]++;

  return TRUE;
}

gboolean
sysprof_capture_writer_add_exit (SysprofCaptureWriter *self,
                                 gint64                time,
                                 gint                  cpu,
                                 gint32                pid)
{
  SysprofCaptureExit *ev;
  gsize len;

  g_assert (self != NULL);

  len = sizeof *ev;
  if (!(ev = sysprof_capture_writer_allocate (self, &len)))
    return FALSE;

  sysprof_capture_writer_frame_init (&ev->frame,
                                     len,
                                     cpu,
                                     pid,
                                     time,
                                     SYSPROF_CAPTURE_FRAME_EXIT);

  self->stat.frame_count[SYSPROF_CAPTURE_FRAME_EXIT]++;

  return TRUE;
}

gboolean
sysprof_capture_writer_add_timestamp (SysprofCaptureWriter *self,
                                      gint64                time,
                                      gint                  cpu,
                                      gint32                pid)
{
  SysprofCaptureTimestamp *ev;
  gsize len;

  g_assert (self != NULL);

  len = sizeof *ev;
  if (!(ev = sysprof_capture_writer_allocate (self, &len)))
    return FALSE;

  sysprof_capture_writer_frame_init (&ev->frame,
                                     len,
                                     cpu,
                                     pid,
                                     time,
                                     SYSPROF_CAPTURE_FRAME_TIMESTAMP);

  self->stat.frame_count[SYSPROF_CAPTURE_FRAME_TIMESTAMP]++;

  return TRUE;
}

static gboolean
sysprof_capture_writer_flush_end_time (SysprofCaptureWriter *self)
{
  SysprofCaptureFileHeader *header;

  g_assert (self != NULL);

  header = mmap_writer_get_file_header (self->writer);
  header->end_time = SYSPROF_CAPTURE_CURRENT_TIME;

  return TRUE;
}

gboolean
sysprof_capture_writer_flush (SysprofCaptureWriter *self)
{
  g_assert (self != NULL);

  return sysprof_capture_writer_flush_jitmap (self) &&
         sysprof_capture_writer_flush_data (self) &&
         sysprof_capture_writer_flush_end_time (self);
}

/**
 * sysprof_capture_writer_save_as:
 * @self: A #SysprofCaptureWriter
 * @filename: the file to save the capture as
 * @error: a location for a #GError or %NULL.
 *
 * Saves the captured data as the file @filename.
 *
 * This is primarily useful if the writer was created with a memory-backed
 * file-descriptor such as a memfd or tmpfs file on Linux.
 *
 * Returns: %TRUE if successful, otherwise %FALSE and @error is set.
 */
gboolean
sysprof_capture_writer_save_as (SysprofCaptureWriter  *self,
                                const gchar           *filename,
                                GError               **error)
{
  gsize to_write;
  off_t in_off;
  off_t pos;
  int writer_fd;
  int fd = -1;

  g_assert (self != NULL);
  g_assert (self->writer != NULL);
  g_assert (filename != NULL);

  if (-1 == (fd = open (filename, O_CREAT | O_RDWR, 0640)))
    goto handle_errno;

  if (!sysprof_capture_writer_flush (self))
    goto handle_errno;

  writer_fd = mmap_writer_get_fd (self->writer);
  pos = mmap_writer_tell (self->writer);

  to_write = pos;
  in_off = 0;

  while (to_write > 0)
    {
      gssize written;

      written = _sysprof_sendfile (fd, writer_fd, &in_off, to_write);

      if (written < 0)
        goto handle_errno;

      if (written == 0 && errno != EAGAIN)
        goto handle_errno;

      g_assert (written <= (gssize)to_write);

      to_write -= written;
    }

  close (fd);

  return TRUE;

handle_errno:
  g_set_error (error,
               G_FILE_ERROR,
               g_file_error_from_errno (errno),
               "%s", g_strerror (errno));

  if (fd != -1)
    {
      close (fd);
      g_unlink (filename);
    }

  return FALSE;
}

/**
 * sysprof_capture_writer_splice:
 * @self: An #SysprofCaptureWriter
 * @dest: An #SysprofCaptureWriter
 * @error: A location for a #GError, or %NULL.
 *
 * This function will copy the capture @self into the capture @dest.  This
 * tries to be semi-efficient by using sendfile() to copy the contents between
 * the captures. @self and @dest will be flushed before the contents are copied
 * into the @dest file-descriptor.
 *
 * Returns: %TRUE if successful, otherwise %FALSE and and @error is set.
 */
gboolean
sysprof_capture_writer_splice (SysprofCaptureWriter  *self,
                               SysprofCaptureWriter  *dest,
                               GError               **error)
{
  SysprofCaptureReader *reader;
  gboolean ret;

  g_assert (self != NULL);
  g_assert (dest != NULL);

  if (!(reader = sysprof_capture_writer_create_reader (self, error)))
    return FALSE;

  ret = sysprof_capture_writer_cat (dest, reader, error);

  sysprof_capture_reader_unref (reader);

  return ret;
}

/**
 * sysprof_capture_writer_create_reader:
 * @self: A #SysprofCaptureWriter
 * @error: a location for a #GError, or %NULL
 *
 * Creates a new reader for the writer.
 *
 * Since readers use positioned reads, this uses the same file-descriptor for
 * the #SysprofCaptureReader. Therefore, if you are writing to the capture while
 * also consuming from the reader, you could get transient failures unless you
 * synchronize the operations.
 *
 * Returns: (transfer full): A #SysprofCaptureReader.
 */
SysprofCaptureReader *
sysprof_capture_writer_create_reader (SysprofCaptureWriter  *self,
                                      GError               **error)
{
  SysprofCaptureReader *ret;
  int fd;
  int copy;

  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (self->writer != NULL, NULL);

  sysprof_capture_writer_flush (self);

  fd = mmap_writer_get_fd (self->writer);

  /*
   * We don't care about the write position, since the reader
   * uses positioned reads.
   */
  if (-1 == (copy = dup (fd)))
    return NULL;

  if ((ret = sysprof_capture_reader_new_from_fd (copy, error)))
    sysprof_capture_reader_set_stat (ret, &self->stat);

  return g_steal_pointer (&ret);
}

/**
 * sysprof_capture_writer_stat:
 * @self: A #SysprofCaptureWriter
 * @stat: (out): A location for an #SysprofCaptureStat
 *
 * This function will fill @stat with statistics generated while capturing
 * the profiler session.
 */
void
sysprof_capture_writer_stat (SysprofCaptureWriter *self,
                             SysprofCaptureStat   *stat)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (stat != NULL);

  *stat = self->stat;
}

gboolean
sysprof_capture_writer_define_counters (SysprofCaptureWriter        *self,
                                        gint64                       time,
                                        gint                         cpu,
                                        gint32                       pid,
                                        const SysprofCaptureCounter *counters,
                                        guint                        n_counters)
{
  SysprofCaptureCounterDefine *def;
  gsize len;
  guint i;

  g_assert (self != NULL);
  g_assert (counters != NULL);

  if (n_counters == 0)
    return TRUE;

  len = sizeof *def + (sizeof *counters * n_counters);
  if (!(def = sysprof_capture_writer_allocate (self, &len)))
    return FALSE;

  sysprof_capture_writer_frame_init (&def->frame,
                                     len,
                                     cpu,
                                     pid,
                                     time,
                                     SYSPROF_CAPTURE_FRAME_CTRDEF);
  def->padding1 = 0;
  def->padding2 = 0;
  def->n_counters = n_counters;

  for (i = 0; i < n_counters; i++)
    {
      if (counters[i].id >= self->next_counter_id)
        {
          g_warning ("Counter %u has not been registered.", counters[i].id);
          continue;
        }

      def->counters[i] = counters[i];
    }

  self->stat.frame_count[SYSPROF_CAPTURE_FRAME_CTRDEF]++;

  return TRUE;
}

gboolean
sysprof_capture_writer_set_counters (SysprofCaptureWriter             *self,
                                     gint64                            time,
                                     gint                              cpu,
                                     gint32                            pid,
                                     const guint                      *counters_ids,
                                     const SysprofCaptureCounterValue *values,
                                     guint                             n_counters)
{
  SysprofCaptureCounterSet *set;
  gsize len;
  guint n_groups;
  guint group;
  guint field;
  guint i;

  g_assert (self != NULL);
  g_assert (counters_ids != NULL || n_counters == 0);
  g_assert (values != NULL || !n_counters);

  if (n_counters == 0)
    return TRUE;

  /* Determine how many value groups we need */
  n_groups = n_counters / G_N_ELEMENTS (set->values[0].values);
  if ((n_groups * G_N_ELEMENTS (set->values[0].values)) != n_counters)
    n_groups++;

  len = sizeof *set + (n_groups * sizeof (SysprofCaptureCounterValues));

  if (!(set = sysprof_capture_writer_allocate (self, &len)))
    return FALSE;

  sysprof_capture_writer_frame_init (&set->frame,
                                     len,
                                     cpu,
                                     pid,
                                     time,
                                     SYSPROF_CAPTURE_FRAME_CTRSET);
  set->padding1 = 0;
  set->padding2 = 0;
  set->n_values = n_groups;

  for (i = 0, group = 0, field = 0; i < n_counters; i++)
    {
      set->values[group].ids[field] = counters_ids[i];
      set->values[group].values[field] = values[i];

      field++;

      if (field == G_N_ELEMENTS (set->values[0].values))
        {
          field = 0;
          group++;
        }
    }

  self->stat.frame_count[SYSPROF_CAPTURE_FRAME_CTRSET]++;

  return TRUE;
}

/**
 * sysprof_capture_writer_request_counter:
 *
 * This requests a series of counter identifiers for the capture.
 *
 * The returning number is always greater than zero. The resulting counter
 * values are monotonic starting from the resulting value.
 *
 * For example, if you are returned 5, and requested 3 counters, the counter
 * ids you should use are 5, 6, and 7.
 *
 * Returns: The next series of counter values or zero on failure.
 */
guint
sysprof_capture_writer_request_counter (SysprofCaptureWriter *self,
                                        guint                 n_counters)
{
  gint ret;

  g_assert (self != NULL);

  if (MAX_COUNTERS - n_counters < self->next_counter_id)
    return 0;

  ret = self->next_counter_id;
  self->next_counter_id += n_counters;

  return ret;
}

gboolean
_sysprof_capture_writer_set_time_range (SysprofCaptureWriter *self,
                                        gint64                start_time,
                                        gint64                end_time)
{
  SysprofCaptureFileHeader *header;

  g_assert (self != NULL);

  if ((header = mmap_writer_get_file_header (self->writer)))
    {
      header->time = start_time;
      header->end_time = end_time;
    }

  return TRUE;
}

SysprofCaptureWriter *
sysprof_capture_writer_new_from_env (gsize buffer_size)
{
  const gchar *fdstr;
  int fd;

  if (!(fdstr = g_getenv ("SYSPROF_TRACE_FD")))
    return NULL;

  /* Make sure the clock is initialized */
  sysprof_clock_init ();

  if (!(fd = atoi (fdstr)))
    return NULL;

  /* ignore stdin/stdout/stderr */
  if (fd < 2)
    return NULL;

  return sysprof_capture_writer_new_from_fd (dup (fd), buffer_size);
}

gsize
sysprof_capture_writer_get_buffer_size (SysprofCaptureWriter *self)
{
  g_return_val_if_fail (self != NULL, 0);

  return mmap_writer_get_buffer_size (self->writer);
}

gboolean
sysprof_capture_writer_add_log (SysprofCaptureWriter *self,
                                gint64                time,
                                gint                  cpu,
                                gint32                pid,
                                GLogLevelFlags        severity,
                                const gchar          *domain,
                                const gchar          *message)
{
  SysprofCaptureLog *ev;
  gsize message_len;
  gsize len;

  g_assert (self != NULL);

  if (domain == NULL)
    domain = "";

  if (message == NULL)
    message = "";
  message_len = strlen (message) + 1;

  len = sizeof *ev + message_len;
  if (!(ev = sysprof_capture_writer_allocate (self, &len)))
    return FALSE;

  sysprof_capture_writer_frame_init (&ev->frame,
                                     len,
                                     cpu,
                                     pid,
                                     time,
                                     SYSPROF_CAPTURE_FRAME_LOG);

  ev->severity = severity & 0xFFFF;
  ev->padding1 = 0;
  ev->padding2 = 0;
  g_strlcpy (ev->domain, domain, sizeof ev->domain);
  memcpy (ev->message, message, message_len);

  self->stat.frame_count[SYSPROF_CAPTURE_FRAME_LOG]++;

  return TRUE;
}

gboolean
sysprof_capture_writer_add_file (SysprofCaptureWriter *self,
                                 gint64                time,
                                 gint                  cpu,
                                 gint32                pid,
                                 const gchar          *path,
                                 gboolean              is_last,
                                 const guint8         *data,
                                 gsize                 data_len)
{
  SysprofCaptureFileChunk *ev;
  gsize len;

  g_assert (self != NULL);

  len = sizeof *ev + data_len;
  if (!(ev = sysprof_capture_writer_allocate (self, &len)))
    return FALSE;

  sysprof_capture_writer_frame_init (&ev->frame,
                                     len,
                                     cpu,
                                     pid,
                                     time,
                                     SYSPROF_CAPTURE_FRAME_FILE_CHUNK);

  ev->padding1 = 0;
  ev->is_last = !!is_last;
  ev->len = data_len;
  g_strlcpy (ev->path, path, sizeof ev->path);
  memcpy (ev->data, data, data_len);

  self->stat.frame_count[SYSPROF_CAPTURE_FRAME_FILE_CHUNK]++;

  return TRUE;
}

gboolean
sysprof_capture_writer_add_file_fd (SysprofCaptureWriter *self,
                                    gint64                time,
                                    gint                  cpu,
                                    gint32                pid,
                                    const gchar          *path,
                                    gint                  fd)
{
  guint8 data[(4096*4L) - sizeof(SysprofCaptureFileChunk)];

  g_assert (self != NULL);

  for (;;)
    {
      gboolean is_last;
      gssize n_read;

    again:
      n_read = read (fd, data, sizeof data);
      if (n_read < 0 && errno == EAGAIN)
        goto again;

      is_last = n_read == 0;

      if (!sysprof_capture_writer_add_file (self, time, cpu, pid, path, is_last, data, n_read))
        return FALSE;

      if (is_last)
        break;
    }

  return TRUE;
}

static gboolean
sysprof_capture_writer_auto_flush_cb (SysprofCaptureWriter *self)
{
  g_assert (self != NULL);

  sysprof_capture_writer_flush (self);

  return G_SOURCE_CONTINUE;
}

void
sysprof_capture_writer_set_flush_delay (SysprofCaptureWriter *self,
                                        GMainContext         *main_context,
                                        guint                 timeout_seconds)
{
  GSource *source;

  g_return_if_fail (self != NULL);

  g_clear_pointer (&self->periodic_flush, g_source_destroy);

  if (timeout_seconds == 0)
    return;

  source = g_timeout_source_new_seconds (timeout_seconds);
  g_source_set_name (source, "[sysprof-capture-writer-flush]");
  g_source_set_priority (source, G_PRIORITY_LOW + 100);
  g_source_set_callback (source,
                         (GSourceFunc) sysprof_capture_writer_auto_flush_cb,
                         self, NULL);

  self->periodic_flush = g_steal_pointer (&source);

  g_source_attach (self->periodic_flush, main_context);
}

gboolean
sysprof_capture_writer_add_allocation (SysprofCaptureWriter  *self,
                                       gint64                 time,
                                       gint                   cpu,
                                       gint32                 pid,
                                       gint32                 tid,
                                       SysprofCaptureAddress  alloc_addr,
                                       gint64                 alloc_size,
                                       SysprofBacktraceFunc   backtrace_func,
                                       gpointer               backtrace_data)
{
  SysprofCaptureAllocation *ev;
  gsize len;
  guint n_addrs;

  g_assert (self != NULL);
  g_assert (backtrace_func != NULL);

  len = sizeof *ev + (MAX_UNWIND_DEPTH * sizeof (SysprofCaptureAddress));
  if (!(ev = sysprof_capture_writer_allocate (self, &len)))
    return FALSE;

  sysprof_capture_writer_frame_init (&ev->frame,
                                     len,
                                     cpu,
                                     pid,
                                     time,
                                     SYSPROF_CAPTURE_FRAME_ALLOCATION);

  ev->alloc_size = alloc_size;
  ev->alloc_addr = alloc_addr;
  ev->padding1 = 0;
  ev->tid = tid;
  ev->n_addrs = 0;

  n_addrs = backtrace_func (ev->addrs, MAX_UNWIND_DEPTH, backtrace_data);

  if (n_addrs <= MAX_UNWIND_DEPTH)
    ev->n_addrs = n_addrs;

  if (ev->n_addrs < MAX_UNWIND_DEPTH)
    {
      gsize diff = (sizeof (SysprofCaptureAddress) * (MAX_UNWIND_DEPTH - ev->n_addrs));

      /* We can simply rewind the length to what we used here because
       * SYSPROF_CAPTURE_ALIGN matches sizeof(SysprofCaptureAddress).
       */
      if (diff > 0)
        {
          ev->frame.len -= diff;
          mmap_writer_rewind (self->writer, diff);
        }
    }

  self->stat.frame_count[SYSPROF_CAPTURE_FRAME_ALLOCATION]++;

  return TRUE;
}

gboolean
sysprof_capture_writer_add_allocation_copy (SysprofCaptureWriter        *self,
                                            gint64                       time,
                                            gint                         cpu,
                                            gint32                       pid,
                                            gint32                       tid,
                                            SysprofCaptureAddress        alloc_addr,
                                            gint64                       alloc_size,
                                            const SysprofCaptureAddress *addrs,
                                            guint                        n_addrs)
{
  SysprofCaptureAllocation *ev;
  gsize len;

  g_assert (self != NULL);

  if (n_addrs > 0xFFF)
    n_addrs = 0xFFF;

  len = sizeof *ev + (n_addrs * sizeof (SysprofCaptureAddress));
  ev = sysprof_capture_writer_allocate (self, &len);
  if (!ev)
    return FALSE;

  sysprof_capture_writer_frame_init (&ev->frame,
                                     len,
                                     cpu,
                                     pid,
                                     time,
                                     SYSPROF_CAPTURE_FRAME_ALLOCATION);

  ev->alloc_size = alloc_size;
  ev->alloc_addr = alloc_addr;
  ev->padding1 = 0;
  ev->tid = tid;
  ev->n_addrs = n_addrs;

  memcpy (ev->addrs, addrs, sizeof (SysprofCaptureAddress) * n_addrs);

  self->stat.frame_count[SYSPROF_CAPTURE_FRAME_ALLOCATION]++;

  return TRUE;
}
