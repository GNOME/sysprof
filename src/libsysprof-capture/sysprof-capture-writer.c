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

#include "sysprof-capture-reader.h"
#include "sysprof-capture-util-private.h"
#include "sysprof-capture-writer.h"

#define DEFAULT_BUFFER_SIZE (_sysprof_getpagesize() * 64L)
#define INVALID_ADDRESS     (G_GUINT64_CONSTANT(0))
#define MAX_COUNTERS        ((1 << 24) - 1)

typedef struct
{
  /* A pinter into the string buffer */
  const gchar *str;

  /* The unique address for the string */
  guint64 addr;
} SysprofCaptureJitmapBucket;

struct _SysprofCaptureWriter
{
  /*
   * This is our buffer location for incoming strings. This is used
   * similarly to GStringChunk except there is only one-page, and after
   * it fills, we flush to disk.
   *
   * This is paired with a closed hash table for deduplication.
   */
  gchar addr_buf[4096*4];

  /* Our hashtable for deduplication. */
  SysprofCaptureJitmapBucket addr_hash[512];

  /* We keep the large fields above so that our allocation will be page
   * alinged for the write buffer. This improves the performance of large
   * writes to the target file-descriptor.
   */
  volatile gint ref_count;

  /*
   * Our address sequence counter. The value that comes from
   * monotonically increasing this is OR'd with JITMAP_MARK to denote
   * the address name should come from the JIT map.
   */
  gsize addr_seq;

  /* Our position in addr_buf. */
  gsize addr_buf_pos;

  /*
   * The number of hash table items in @addr_hash. This is an
   * optimization so that we can avoid calculating the number of strings
   * when flushing out the jitmap.
   */
  guint addr_hash_size;

  /* Capture file handle */
  int fd;

  /* Our write buffer for fd */
  guint8 *buf;
  gsize pos;
  gsize len;

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
  g_assert (frame_ != NULL);

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
      sysprof_capture_writer_flush (self);

      if (self->fd != -1)
        {
          close (self->fd);
          self->fd = -1;
        }

      g_free (self->buf);
      g_free (self);
    }
}

SysprofCaptureWriter *
sysprof_capture_writer_ref (SysprofCaptureWriter *self)
{
  g_assert (self != NULL);
  g_assert (self->ref_count > 0);

  g_atomic_int_inc (&self->ref_count);

  return self;
}

void
sysprof_capture_writer_unref (SysprofCaptureWriter *self)
{
  g_assert (self != NULL);
  g_assert (self->ref_count > 0);

  if (g_atomic_int_dec_and_test (&self->ref_count))
    sysprof_capture_writer_finalize (self);
}

static gboolean
sysprof_capture_writer_flush_data (SysprofCaptureWriter *self)
{
  const guint8 *buf;
  gssize written;
  gsize to_write;

  g_assert (self != NULL);
  g_assert (self->pos <= self->len);
  g_assert ((self->pos % SYSPROF_CAPTURE_ALIGN) == 0);

  if (self->pos == 0)
    return TRUE;

  buf = self->buf;
  to_write = self->pos;

  while (to_write > 0)
    {
      written = _sysprof_write (self->fd, buf, to_write);
      if (written < 0)
        return FALSE;

      if (written == 0 && errno != EAGAIN)
        return FALSE;

      g_assert (written <= (gssize)to_write);

      buf += written;
      to_write -= written;
    }

  self->pos = 0;

  return TRUE;
}

static inline void
sysprof_capture_writer_realign (gsize *pos)
{
  *pos = (*pos + SYSPROF_CAPTURE_ALIGN - 1) & ~(SYSPROF_CAPTURE_ALIGN - 1);
}

static inline gboolean
sysprof_capture_writer_ensure_space_for (SysprofCaptureWriter *self,
                                         gsize                 len)
{
  /* Check for max frame size */
  if (len > G_MAXUSHORT)
    return FALSE;

  if ((self->len - self->pos) < len)
    {
      if (!sysprof_capture_writer_flush_data (self))
        return FALSE;
    }

  return TRUE;
}

static inline gpointer
sysprof_capture_writer_allocate (SysprofCaptureWriter *self,
                                 gsize                *len)
{
  gpointer p;

  g_assert (self != NULL);
  g_assert (len != NULL);
  g_assert ((self->pos % SYSPROF_CAPTURE_ALIGN) == 0);

  sysprof_capture_writer_realign (len);

  if (!sysprof_capture_writer_ensure_space_for (self, *len))
    return NULL;

  p = (gpointer)&self->buf[self->pos];

  self->pos += *len;

  g_assert ((self->pos % SYSPROF_CAPTURE_ALIGN) == 0);

  return p;
}

static gboolean
sysprof_capture_writer_flush_jitmap (SysprofCaptureWriter *self)
{
  SysprofCaptureJitmap jitmap;
  gssize r;
  gsize len;

  g_assert (self != NULL);

  if (self->addr_hash_size == 0)
    return TRUE;

  g_assert (self->addr_buf_pos > 0);

  len = sizeof jitmap + self->addr_buf_pos;

  sysprof_capture_writer_realign (&len);

  sysprof_capture_writer_frame_init (&jitmap.frame,
                                     len,
                                     -1,
                                     _sysprof_getpid (),
                                     SYSPROF_CAPTURE_CURRENT_TIME,
                                     SYSPROF_CAPTURE_FRAME_JITMAP);
  jitmap.n_jitmaps = self->addr_hash_size;

  if (sizeof jitmap != _sysprof_write (self->fd, &jitmap, sizeof jitmap))
    return FALSE;

  r = _sysprof_write (self->fd, self->addr_buf, len - sizeof jitmap);
  if (r < 0 || (gsize)r != (len - sizeof jitmap))
    return FALSE;

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
  g_assert ((self->pos % SYSPROF_CAPTURE_ALIGN) == 0);

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
  SysprofCaptureWriter *self;
  SysprofCaptureFileHeader *header;
  GTimeVal tv;
  gsize header_len = sizeof(*header);

  if (fd < 0)
    return NULL;

  if (buffer_size == 0)
    buffer_size = DEFAULT_BUFFER_SIZE;

  g_assert (fd != -1);
  g_assert (buffer_size % _sysprof_getpagesize() == 0);

  /* This is only useful on files, memfd, etc */
  if (ftruncate (fd, 0) != 0) { /* Do Nothing */ }

  self = g_new0 (SysprofCaptureWriter, 1);
  self->ref_count = 1;
  self->fd = fd;
  self->buf = (guint8 *)g_malloc0 (buffer_size);
  self->len = buffer_size;
  self->next_counter_id = 1;

  g_get_current_time (&tv);
  nowstr = g_time_val_to_iso8601 (&tv);

  header = sysprof_capture_writer_allocate (self, &header_len);

  if (header == NULL)
    {
      sysprof_capture_writer_finalize (self);
      return NULL;
    }

  header->magic = SYSPROF_CAPTURE_MAGIC;
  header->version = 1;
#ifdef G_LITTLE_ENDIAN
  header->little_endian = TRUE;
#else
  header->little_endian = FALSE;
#endif
  header->padding = 0;
  g_strlcpy (header->capture_time, nowstr, sizeof header->capture_time);
  header->time = SYSPROF_CAPTURE_CURRENT_TIME;
  header->end_time = 0;
  memset (header->suffix, 0, sizeof header->suffix);

  if (!sysprof_capture_writer_flush_data (self))
    {
      sysprof_capture_writer_finalize (self);
      return NULL;
    }

  g_assert (self->pos == 0);
  g_assert (self->len > 0);
  g_assert (self->len % _sysprof_getpagesize() == 0);
  g_assert (self->buf != NULL);
  g_assert (self->addr_hash_size == 0);
  g_assert (self->fd != -1);

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

  ev = (SysprofCaptureMap *)sysprof_capture_writer_allocate (self, &len);
  if (!ev)
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
  ev = (SysprofCaptureMark *)sysprof_capture_writer_allocate (self, &len);
  if (!ev)
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
  ev = (SysprofCaptureMetadata *)sysprof_capture_writer_allocate (self, &len);
  if (!ev)
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

  ev = (SysprofCaptureProcess *)sysprof_capture_writer_allocate (self, &len);
  if (!ev)
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

  ev = (SysprofCaptureSample *)sysprof_capture_writer_allocate (self, &len);
  if (!ev)
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
  gsize len = sizeof *ev;

  g_assert (self != NULL);

  ev = (SysprofCaptureFork *)sysprof_capture_writer_allocate (self, &len);
  if (!ev)
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
  gsize len = sizeof *ev;

  g_assert (self != NULL);

  ev = (SysprofCaptureExit *)sysprof_capture_writer_allocate (self, &len);
  if (!ev)
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
  gsize len = sizeof *ev;

  g_assert (self != NULL);

  ev = (SysprofCaptureTimestamp *)sysprof_capture_writer_allocate (self, &len);
  if (!ev)
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
  gint64 end_time = SYSPROF_CAPTURE_CURRENT_TIME;
  ssize_t ret;

  g_assert (self != NULL);

  /* This field is opportunistic, so a failure is okay. */

again:
  ret = _sysprof_pwrite (self->fd,
                         &end_time,
                         sizeof (end_time),
                         G_STRUCT_OFFSET (SysprofCaptureFileHeader, end_time));

  if (ret < 0 && errno == EAGAIN)
    goto again;

  return TRUE;
}

gboolean
sysprof_capture_writer_flush (SysprofCaptureWriter *self)
{
  g_assert (self != NULL);

  return (sysprof_capture_writer_flush_jitmap (self) &&
          sysprof_capture_writer_flush_data (self) &&
          sysprof_capture_writer_flush_end_time (self));
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
  int fd = -1;

  g_assert (self != NULL);
  g_assert (self->fd != -1);
  g_assert (filename != NULL);

  if (-1 == (fd = open (filename, O_CREAT | O_RDWR, 0640)))
    goto handle_errno;

  if (!sysprof_capture_writer_flush (self))
    goto handle_errno;

  if (-1 == (pos = lseek (self->fd, 0L, SEEK_CUR)))
    goto handle_errno;

  to_write = pos;
  in_off = 0;

  while (to_write > 0)
    {
      gssize written;

      written = _sysprof_sendfile (fd, self->fd, &in_off, pos);

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
 * _sysprof_capture_writer_splice_from_fd:
 * @self: An #SysprofCaptureWriter
 * @fd: the fd to read from.
 * @error: A location for a #GError, or %NULL.
 *
 * This is internal API for SysprofCaptureWriter and SysprofCaptureReader to
 * communicate when splicing a reader into a writer.
 *
 * This should not be used outside of #SysprofCaptureReader or
 * #SysprofCaptureWriter.
 *
 * This will not advance the position of @fd.
 *
 * Returns: %TRUE if successful; otherwise %FALSE and @error is set.
 */
gboolean
_sysprof_capture_writer_splice_from_fd (SysprofCaptureWriter  *self,
                                        int                    fd,
                                        GError               **error)
{
  struct stat stbuf;
  off_t in_off;
  gsize to_write;

  g_assert (self != NULL);
  g_assert (self->fd != -1);

  if (-1 == fstat (fd, &stbuf))
    goto handle_errno;

  if (stbuf.st_size < 256)
    {
      g_set_error (error,
                   G_FILE_ERROR,
                   G_FILE_ERROR_INVAL,
                   "Cannot splice, possibly corrupt file.");
      return FALSE;
    }

  in_off = 256;
  to_write = stbuf.st_size - in_off;

  while (to_write > 0)
    {
      gssize written;

      written = _sysprof_sendfile (self->fd, fd, &in_off, to_write);

      if (written < 0)
        goto handle_errno;

      if (written == 0 && errno != EAGAIN)
        goto handle_errno;

      g_assert (written <= (gssize)to_write);

      to_write -= written;
    }

  return TRUE;

handle_errno:
  g_set_error (error,
               G_FILE_ERROR,
               g_file_error_from_errno (errno),
               "%s", g_strerror (errno));

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
  gboolean ret;
  off_t pos;

  g_assert (self != NULL);
  g_assert (self->fd != -1);
  g_assert (dest != NULL);
  g_assert (dest->fd != -1);

  /* Flush before writing anything to ensure consistency */
  if (!sysprof_capture_writer_flush (self) || !sysprof_capture_writer_flush (dest))
    goto handle_errno;

  /* Track our current position so we can reset */
  if ((off_t)-1 == (pos = lseek (self->fd, 0L, SEEK_CUR)))
    goto handle_errno;

  /* Perform the splice */
  ret = _sysprof_capture_writer_splice_from_fd (dest, self->fd, error);

  /* Now reset or file-descriptor position (it should be the same */
  if (pos != lseek (self->fd, pos, SEEK_SET))
    {
      ret = FALSE;
      goto handle_errno;
    }

  return ret;

handle_errno:
  g_set_error (error,
               G_FILE_ERROR,
               g_file_error_from_errno (errno),
               "%s", g_strerror (errno));

  return FALSE;
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
  int copy;

  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (self->fd != -1, NULL);

  if (!sysprof_capture_writer_flush (self))
    {
      g_set_error (error,
                   G_FILE_ERROR,
                   g_file_error_from_errno (errno),
                   "%s", g_strerror (errno));
      return NULL;
    }

  /*
   * We don't care about the write position, since the reader
   * uses positioned reads.
   */
  if (-1 == (copy = dup (self->fd)))
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

  def = (SysprofCaptureCounterDefine *)sysprof_capture_writer_allocate (self, &len);
  if (!def)
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
  g_assert (counters_ids != NULL);
  g_assert (values != NULL || !n_counters);

  if (n_counters == 0)
    return TRUE;

  /* Determine how many value groups we need */
  n_groups = n_counters / G_N_ELEMENTS (set->values[0].values);
  if ((n_groups * G_N_ELEMENTS (set->values[0].values)) != n_counters)
    n_groups++;

  len = sizeof *set + (n_groups * sizeof (SysprofCaptureCounterValues));

  set = (SysprofCaptureCounterSet *)sysprof_capture_writer_allocate (self, &len);
  if (!set)
    return FALSE;

  memset (set, 0, len);

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
  ssize_t ret;

  g_assert (self != NULL);

do_start:
  ret = _sysprof_pwrite (self->fd,
                         &start_time,
                         sizeof (start_time),
                         G_STRUCT_OFFSET (SysprofCaptureFileHeader, time));

  if (ret < 0 && errno == EAGAIN)
    goto do_start;

do_end:
  ret = _sysprof_pwrite (self->fd,
                         &end_time,
                         sizeof (end_time),
                         G_STRUCT_OFFSET (SysprofCaptureFileHeader, end_time));

  if (ret < 0 && errno == EAGAIN)
    goto do_end;

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

  return self->len;
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
  ev = (SysprofCaptureLog *)sysprof_capture_writer_allocate (self, &len);
  if (!ev)
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
  ev = (SysprofCaptureFileChunk *)sysprof_capture_writer_allocate (self, &len);
  if (!ev)
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
