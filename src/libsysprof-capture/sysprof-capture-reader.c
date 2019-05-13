/* sysprof-capture-reader.c
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
 *
 * This file is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This file is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "sysprof-capture-reader"

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "sysprof-capture-reader.h"
#include "sysprof-capture-util-private.h"
#include "sysprof-capture-writer.h"

struct _SysprofCaptureReader
{
  volatile gint             ref_count;
  gchar                    *filename;
  guint8                   *buf;
  gsize                     bufsz;
  gsize                     len;
  gsize                     pos;
  gsize                     fd_off;
  int                       fd;
  gint                      endian;
  SysprofCaptureFileHeader  header;
  gint64                    end_time;
};

static gboolean
sysprof_capture_reader_read_file_header (SysprofCaptureReader      *self,
                                         SysprofCaptureFileHeader  *header,
                                         GError                   **error)
{
  g_assert (self != NULL);
  g_assert (header != NULL);

  if (sizeof *header != _sysprof_pread (self->fd, header, sizeof *header, 0L))
    {
      g_set_error (error,
                   G_FILE_ERROR,
                   g_file_error_from_errno (errno),
                   "%s", g_strerror (errno));
      return FALSE;
    }

  if (header->magic != SYSPROF_CAPTURE_MAGIC)
    {
      g_set_error (error,
                   G_FILE_ERROR,
                   G_FILE_ERROR_FAILED,
                   "Capture file magic does not match");
      return FALSE;
    }

  header->capture_time[sizeof header->capture_time - 1] = '\0';

  return TRUE;
}

static void
sysprof_capture_reader_finalize (SysprofCaptureReader *self)
{
  if (self != NULL)
    {
      close (self->fd);
      g_free (self->buf);
      g_free (self->filename);
      g_free (self);
    }
}

const gchar *
sysprof_capture_reader_get_time (SysprofCaptureReader *self)
{
  g_return_val_if_fail (self != NULL, NULL);

  return self->header.capture_time;
}

const gchar *
sysprof_capture_reader_get_filename (SysprofCaptureReader *self)
{
  g_return_val_if_fail (self != NULL, NULL);

  return self->filename;
}

/**
 * sysprof_capture_reader_new_from_fd:
 * @fd: an fd to take ownership from
 * @error: a location for a #GError or %NULL
 *
 * Creates a new reader using the file-descriptor.
 *
 * This is useful if you don't necessarily have access to the filename itself.
 *
 * Returns: (transfer full): an #SysprofCaptureReader or %NULL upon failure.
 */
SysprofCaptureReader *
sysprof_capture_reader_new_from_fd (int      fd,
                                    GError **error)
{
  SysprofCaptureReader *self;

  g_assert (fd > -1);

  self = g_new0 (SysprofCaptureReader, 1);
  self->ref_count = 1;
  self->bufsz = G_MAXUSHORT * 2;
  self->buf = g_malloc (self->bufsz);
  self->len = 0;
  self->pos = 0;
  self->fd = fd;
  self->fd_off = sizeof (SysprofCaptureFileHeader);

  if (!sysprof_capture_reader_read_file_header (self, &self->header, error))
    {
      sysprof_capture_reader_finalize (self);
      return NULL;
    }

  if (self->header.little_endian)
    self->endian = G_LITTLE_ENDIAN;
  else
    self->endian = G_BIG_ENDIAN;

  return self;
}

SysprofCaptureReader *
sysprof_capture_reader_new (const gchar  *filename,
                            GError      **error)
{
  SysprofCaptureReader *self;
  int fd;

  g_assert (filename != NULL);

  if (-1 == (fd = open (filename, O_RDONLY, 0)))
    {
      g_set_error (error,
                   G_FILE_ERROR,
                   g_file_error_from_errno (errno),
                   "%s", g_strerror (errno));
      return NULL;
    }

  if (NULL == (self = sysprof_capture_reader_new_from_fd (fd, error)))
    {
      close (fd);
      return NULL;
    }

  self->filename = g_strdup (filename);

  return self;
}

static inline void
sysprof_capture_reader_bswap_frame (SysprofCaptureReader *self,
                                    SysprofCaptureFrame  *frame)
{
  g_assert (self != NULL);
  g_assert (frame!= NULL);

  if (G_UNLIKELY (self->endian != G_BYTE_ORDER))
    {
      frame->len = GUINT16_SWAP_LE_BE (frame->len);
      frame->cpu = GUINT16_SWAP_LE_BE (frame->cpu);
      frame->pid = GUINT32_SWAP_LE_BE (frame->pid);
      frame->time = GUINT64_SWAP_LE_BE (frame->time);
    }
}

static inline void
sysprof_capture_reader_bswap_map (SysprofCaptureReader *self,
                                  SysprofCaptureMap    *map)
{
  g_assert (self != NULL);
  g_assert (map != NULL);

  if (G_UNLIKELY (self->endian != G_BYTE_ORDER))
    {
      map->start = GUINT64_SWAP_LE_BE (map->start);
      map->end = GUINT64_SWAP_LE_BE (map->end);
      map->offset = GUINT64_SWAP_LE_BE (map->offset);
      map->inode = GUINT64_SWAP_LE_BE (map->inode);
    }
}

static inline void
sysprof_capture_reader_bswap_mark (SysprofCaptureReader *self,
                                   SysprofCaptureMark   *mark)
{
  g_assert (self != NULL);
  g_assert (mark != NULL);

  if (G_UNLIKELY (self->endian != G_BYTE_ORDER))
    mark->duration = GUINT64_SWAP_LE_BE (mark->duration);
}

static inline void
sysprof_capture_reader_bswap_jitmap (SysprofCaptureReader *self,
                                     SysprofCaptureJitmap *jitmap)
{
  g_assert (self != NULL);
  g_assert (jitmap != NULL);

  if (G_UNLIKELY (self->endian != G_BYTE_ORDER))
    jitmap->n_jitmaps = GUINT64_SWAP_LE_BE (jitmap->n_jitmaps);
}

static gboolean
sysprof_capture_reader_ensure_space_for (SysprofCaptureReader *self,
                                         gsize                 len)
{
  g_assert (self != NULL);
  g_assert (self->pos <= self->len);
  g_assert (len > 0);

  if ((self->len - self->pos) < len)
    {
      gssize r;

      if (self->len > self->pos)
        memmove (self->buf, &self->buf[self->pos], self->len - self->pos);
      self->len -= self->pos;
      self->pos = 0;

      while (self->len < len)
        {
          g_assert ((self->pos + self->len) < self->bufsz);
          g_assert (self->len < self->bufsz);

          /* Read into our buffer after our current read position */
          r = _sysprof_pread (self->fd,
                         &self->buf[self->len],
                         self->bufsz - self->len,
                         self->fd_off);

          if (r <= 0)
            break;

          self->fd_off += r;
          self->len += r;
        }
    }

  return (self->len - self->pos) >= len;
}

gboolean
sysprof_capture_reader_skip (SysprofCaptureReader *self)
{
  SysprofCaptureFrame *frame;

  g_assert (self != NULL);
  g_assert ((self->pos % SYSPROF_CAPTURE_ALIGN) == 0);

  if (!sysprof_capture_reader_ensure_space_for (self, sizeof (SysprofCaptureFrame)))
    return FALSE;

  frame = (SysprofCaptureFrame *)(gpointer)&self->buf[self->pos];
  sysprof_capture_reader_bswap_frame (self, frame);

  if (frame->len < sizeof (SysprofCaptureFrame))
    return FALSE;

  if (!sysprof_capture_reader_ensure_space_for (self, frame->len))
    return FALSE;

  frame = (SysprofCaptureFrame *)(gpointer)&self->buf[self->pos];

  self->pos += frame->len;

  if ((self->pos % SYSPROF_CAPTURE_ALIGN) != 0)
    return FALSE;

  return TRUE;
}

gboolean
sysprof_capture_reader_peek_frame (SysprofCaptureReader *self,
                                   SysprofCaptureFrame  *frame)
{
  SysprofCaptureFrame *real_frame;

  g_assert (self != NULL);
  g_assert ((self->pos % SYSPROF_CAPTURE_ALIGN) == 0);
  g_assert (self->pos <= self->len);
  g_assert (self->pos <= self->bufsz);

  if (!sysprof_capture_reader_ensure_space_for (self, sizeof *real_frame))
    return FALSE;

  g_assert ((self->pos % SYSPROF_CAPTURE_ALIGN) == 0);

  real_frame = (SysprofCaptureFrame *)(gpointer)&self->buf[self->pos];

  *frame = *real_frame;

  sysprof_capture_reader_bswap_frame (self, frame);

  if (frame->time > self->end_time)
    self->end_time = frame->time;

  return TRUE;
}

gboolean
sysprof_capture_reader_peek_type (SysprofCaptureReader    *self,
                                  SysprofCaptureFrameType *type)
{
  SysprofCaptureFrame frame;

  g_assert (self != NULL);
  g_assert (type != NULL);

  if (!sysprof_capture_reader_peek_frame (self, &frame))
    return FALSE;

  *type = frame.type;

  return TRUE;
}

static const SysprofCaptureFrame *
sysprof_capture_reader_read_basic (SysprofCaptureReader    *self,
                                   SysprofCaptureFrameType  type,
                                   gsize                    extra)
{
  SysprofCaptureFrame *frame;
  gsize len = sizeof *frame + extra;

  g_assert (self != NULL);
  g_assert ((self->pos % SYSPROF_CAPTURE_ALIGN) == 0);
  g_assert (self->pos <= self->bufsz);

  if (!sysprof_capture_reader_ensure_space_for (self, len))
    return NULL;

  frame = (SysprofCaptureFrame *)(gpointer)&self->buf[self->pos];

  sysprof_capture_reader_bswap_frame (self, frame);

  if (frame->len < len)
    return NULL;

  if (frame->type != type)
    return NULL;

  self->pos += frame->len;

  return frame;
}

const SysprofCaptureTimestamp *
sysprof_capture_reader_read_timestamp (SysprofCaptureReader *self)
{
  return (SysprofCaptureTimestamp *)
    sysprof_capture_reader_read_basic (self, SYSPROF_CAPTURE_FRAME_TIMESTAMP, 0);
}

const SysprofCaptureExit *
sysprof_capture_reader_read_exit (SysprofCaptureReader *self)
{
  return (SysprofCaptureExit *)
    sysprof_capture_reader_read_basic (self, SYSPROF_CAPTURE_FRAME_EXIT, 0);
}

const SysprofCaptureFork *
sysprof_capture_reader_read_fork (SysprofCaptureReader *self)
{
  SysprofCaptureFork *fk;

  g_assert (self != NULL);

  fk = (SysprofCaptureFork *)
    sysprof_capture_reader_read_basic (self, SYSPROF_CAPTURE_FRAME_FORK, sizeof(guint32));

  if (fk != NULL)
    {
      if (G_UNLIKELY (self->endian != G_BYTE_ORDER))
        fk->child_pid = GUINT32_SWAP_LE_BE (fk->child_pid);
    }

  return fk;
}

const SysprofCaptureMap *
sysprof_capture_reader_read_map (SysprofCaptureReader *self)
{
  SysprofCaptureMap *map;

  g_assert (self != NULL);
  g_assert ((self->pos % SYSPROF_CAPTURE_ALIGN) == 0);
  g_assert (self->pos <= self->bufsz);

  if (!sysprof_capture_reader_ensure_space_for (self, sizeof *map))
    return NULL;

  map = (SysprofCaptureMap *)(gpointer)&self->buf[self->pos];

  sysprof_capture_reader_bswap_frame (self, &map->frame);

  if (map->frame.type != SYSPROF_CAPTURE_FRAME_MAP)
    return NULL;

  if (map->frame.len < (sizeof *map + 1))
    return NULL;

  if (!sysprof_capture_reader_ensure_space_for (self, map->frame.len))
    return NULL;

  map = (SysprofCaptureMap *)(gpointer)&self->buf[self->pos];

  if (self->buf[self->pos + map->frame.len - 1] != '\0')
    return NULL;

  sysprof_capture_reader_bswap_map (self, map);

  self->pos += map->frame.len;

  if ((self->pos % SYSPROF_CAPTURE_ALIGN) != 0)
    return NULL;

  return map;
}

const SysprofCaptureMark *
sysprof_capture_reader_read_mark (SysprofCaptureReader *self)
{
  SysprofCaptureMark *mark;

  g_assert (self != NULL);
  g_assert ((self->pos % SYSPROF_CAPTURE_ALIGN) == 0);
  g_assert (self->pos <= self->bufsz);

  if (!sysprof_capture_reader_ensure_space_for (self, sizeof *mark))
    return NULL;

  mark = (SysprofCaptureMark *)(gpointer)&self->buf[self->pos];

  sysprof_capture_reader_bswap_frame (self, &mark->frame);

  if (mark->frame.type != SYSPROF_CAPTURE_FRAME_MARK)
    return NULL;

  if (mark->frame.len < (sizeof *mark + 1))
    return NULL;

  if (!sysprof_capture_reader_ensure_space_for (self, mark->frame.len))
    return NULL;

  mark = (SysprofCaptureMark *)(gpointer)&self->buf[self->pos];

  sysprof_capture_reader_bswap_mark (self, mark);

  self->pos += mark->frame.len;

  if ((self->pos % SYSPROF_CAPTURE_ALIGN) != 0)
    return NULL;

  /* Ensure trailing \0 in name and message */
  mark->name[sizeof mark->name - 1] = 0;
  self->buf[self->pos + mark->frame.len - 1] = 0;

  /* Maybe update end-time */
  if G_UNLIKELY ((mark->frame.time + mark->duration) > self->end_time)
    self->end_time = mark->frame.time + mark->duration;

  return mark;
}

const SysprofCaptureProcess *
sysprof_capture_reader_read_process (SysprofCaptureReader *self)
{
  SysprofCaptureProcess *process;

  g_assert (self != NULL);
  g_assert ((self->pos % SYSPROF_CAPTURE_ALIGN) == 0);
  g_assert (self->pos <= self->bufsz);

  if (!sysprof_capture_reader_ensure_space_for (self, sizeof *process))
    return NULL;

  process = (SysprofCaptureProcess *)(gpointer)&self->buf[self->pos];

  sysprof_capture_reader_bswap_frame (self, &process->frame);

  if (process->frame.type != SYSPROF_CAPTURE_FRAME_PROCESS)
    return NULL;

  if (process->frame.len < (sizeof *process + 1))
    return NULL;

  if (!sysprof_capture_reader_ensure_space_for (self, process->frame.len))
    return NULL;

  process = (SysprofCaptureProcess *)(gpointer)&self->buf[self->pos];

  if (self->buf[self->pos + process->frame.len - 1] != '\0')
    return NULL;

  self->pos += process->frame.len;

  if ((self->pos % SYSPROF_CAPTURE_ALIGN) != 0)
    return NULL;

  return process;
}

GHashTable *
sysprof_capture_reader_read_jitmap (SysprofCaptureReader *self)
{
  g_autoptr(GHashTable) ret = NULL;
  SysprofCaptureJitmap *jitmap;
  guint8 *buf;
  guint8 *endptr;
  guint i;

  g_assert (self != NULL);
  g_assert ((self->pos % SYSPROF_CAPTURE_ALIGN) == 0);
  g_assert (self->pos <= self->bufsz);

  if (!sysprof_capture_reader_ensure_space_for (self, sizeof *jitmap))
    return NULL;

  jitmap = (SysprofCaptureJitmap *)(gpointer)&self->buf[self->pos];

  sysprof_capture_reader_bswap_frame (self, &jitmap->frame);

  if (jitmap->frame.type != SYSPROF_CAPTURE_FRAME_JITMAP)
    return NULL;

  if (jitmap->frame.len < sizeof *jitmap)
    return NULL;

  if (!sysprof_capture_reader_ensure_space_for (self, jitmap->frame.len))
    return NULL;

  jitmap = (SysprofCaptureJitmap *)(gpointer)&self->buf[self->pos];

  ret = g_hash_table_new_full (NULL, NULL, NULL, g_free);

  buf = jitmap->data;
  endptr = &self->buf[self->pos + jitmap->frame.len];

  for (i = 0; i < jitmap->n_jitmaps; i++)
    {
      SysprofCaptureAddress addr;
      const gchar *str;

      if (buf + sizeof addr >= endptr)
        return NULL;

      memcpy (&addr, buf, sizeof addr);
      buf += sizeof addr;

      str = (gchar *)buf;

      buf = memchr (buf, '\0', (endptr - buf));

      if (buf == NULL)
        return NULL;

      buf++;

      g_hash_table_insert (ret, GSIZE_TO_POINTER (addr), g_strdup (str));
    }

  sysprof_capture_reader_bswap_jitmap (self, jitmap);

  self->pos += jitmap->frame.len;

  return g_steal_pointer (&ret);
}

const SysprofCaptureSample *
sysprof_capture_reader_read_sample (SysprofCaptureReader *self)
{
  SysprofCaptureSample *sample;

  g_assert (self != NULL);
  g_assert ((self->pos % SYSPROF_CAPTURE_ALIGN) == 0);
  g_assert (self->pos <= self->bufsz);

  if (!sysprof_capture_reader_ensure_space_for (self, sizeof *sample))
    return NULL;

  sample = (SysprofCaptureSample *)(gpointer)&self->buf[self->pos];

  sysprof_capture_reader_bswap_frame (self, &sample->frame);

  if (sample->frame.type != SYSPROF_CAPTURE_FRAME_SAMPLE)
    return NULL;

  if (sample->frame.len < sizeof *sample)
    return NULL;

  if (self->endian != G_BYTE_ORDER)
    sample->n_addrs = GUINT16_SWAP_LE_BE (sample->n_addrs);

  if (sample->frame.len < (sizeof *sample + (sizeof(SysprofCaptureAddress) * sample->n_addrs)))
    return NULL;

  if (!sysprof_capture_reader_ensure_space_for (self, sample->frame.len))
    return NULL;

  sample = (SysprofCaptureSample *)(gpointer)&self->buf[self->pos];

  if (G_UNLIKELY (self->endian != G_BYTE_ORDER))
    {
      guint i;

      for (i = 0; i < sample->n_addrs; i++)
        sample->addrs[i] = GUINT64_SWAP_LE_BE (sample->addrs[i]);
    }

  self->pos += sample->frame.len;

  return sample;
}

const SysprofCaptureFrameCounterDefine *
sysprof_capture_reader_read_counter_define (SysprofCaptureReader *self)
{
  SysprofCaptureFrameCounterDefine *def;

  g_assert (self != NULL);
  g_assert ((self->pos % SYSPROF_CAPTURE_ALIGN) == 0);
  g_assert (self->pos <= self->bufsz);

  if (!sysprof_capture_reader_ensure_space_for (self, sizeof *def))
    return NULL;

  def = (SysprofCaptureFrameCounterDefine *)(gpointer)&self->buf[self->pos];

  if (def->frame.type != SYSPROF_CAPTURE_FRAME_CTRDEF)
    return NULL;

  if (def->frame.len < sizeof *def)
    return NULL;

  if (G_UNLIKELY (self->endian != G_BYTE_ORDER))
    def->n_counters = GUINT16_SWAP_LE_BE (def->n_counters);

  if (def->frame.len < (sizeof *def + (sizeof (SysprofCaptureFrameCounterDefine) * def->n_counters)))
    return NULL;

  if (!sysprof_capture_reader_ensure_space_for (self, def->frame.len))
    return NULL;

  def = (SysprofCaptureFrameCounterDefine *)(gpointer)&self->buf[self->pos];

  if (G_UNLIKELY (self->endian != G_BYTE_ORDER))
    {
      guint i;

      for (i = 0; i < def->n_counters; i++)
        {
          def->counters[i].id = GUINT32_SWAP_LE_BE (def->counters[i].id);
          def->counters[i].value.v64 = GUINT64_SWAP_LE_BE (def->counters[i].value.v64);
        }
    }

  self->pos += def->frame.len;

  return def;
}

const SysprofCaptureFrameCounterSet *
sysprof_capture_reader_read_counter_set (SysprofCaptureReader *self)
{
  SysprofCaptureFrameCounterSet *set;

  g_assert (self != NULL);
  g_assert ((self->pos % SYSPROF_CAPTURE_ALIGN) == 0);
  g_assert (self->pos <= self->bufsz);

  if (!sysprof_capture_reader_ensure_space_for (self, sizeof *set))
    return NULL;

  set = (SysprofCaptureFrameCounterSet *)(gpointer)&self->buf[self->pos];

  if (set->frame.type != SYSPROF_CAPTURE_FRAME_CTRSET)
    return NULL;

  if (set->frame.len < sizeof *set)
    return NULL;

  if (self->endian != G_BYTE_ORDER)
    set->n_values = GUINT16_SWAP_LE_BE (set->n_values);

  if (set->frame.len < (sizeof *set + (sizeof (SysprofCaptureCounterValues) * set->n_values)))
    return NULL;

  if (!sysprof_capture_reader_ensure_space_for (self, set->frame.len))
    return NULL;

  set = (SysprofCaptureFrameCounterSet *)(gpointer)&self->buf[self->pos];

  if (G_UNLIKELY (self->endian != G_BYTE_ORDER))
    {
      guint i;

      for (i = 0; i < set->n_values; i++)
        {
          guint j;

          for (j = 0; j < G_N_ELEMENTS (set->values[0].values); j++)
            {
              set->values[i].ids[j] = GUINT32_SWAP_LE_BE (set->values[i].ids[j]);
              set->values[i].values[j].v64 = GUINT64_SWAP_LE_BE (set->values[i].values[j].v64);
            }
        }
    }

  self->pos += set->frame.len;

  return set;
}

gboolean
sysprof_capture_reader_reset (SysprofCaptureReader *self)
{
  g_assert (self != NULL);

  self->fd_off = sizeof (SysprofCaptureFileHeader);
  self->pos = 0;
  self->len = 0;

  return TRUE;
}

SysprofCaptureReader *
sysprof_capture_reader_ref (SysprofCaptureReader *self)
{
  g_assert (self != NULL);
  g_assert (self->ref_count > 0);

  g_atomic_int_inc (&self->ref_count);

  return self;
}

void
sysprof_capture_reader_unref (SysprofCaptureReader *self)
{
  g_assert (self != NULL);
  g_assert (self->ref_count > 0);

  if (g_atomic_int_dec_and_test (&self->ref_count))
    sysprof_capture_reader_finalize (self);
}

gboolean
sysprof_capture_reader_splice (SysprofCaptureReader  *self,
                               SysprofCaptureWriter  *dest,
                               GError               **error)
{
  g_assert (self != NULL);
  g_assert (self->fd != -1);
  g_assert (dest != NULL);

  /* Flush before writing anything to ensure consistency */
  if (!sysprof_capture_writer_flush (dest))
    {
      g_set_error (error,
                   G_FILE_ERROR,
                   g_file_error_from_errno (errno),
                   "%s", g_strerror (errno));
      return FALSE;
    }

  /*
   * We don't need to track position because writer will
   * track the current position to avoid reseting it.
   */

  /* Perform the splice */
  return _sysprof_capture_writer_splice_from_fd (dest, self->fd, error);
}

/**
 * sysprof_capture_reader_save_as:
 * @self: An #SysprofCaptureReader
 * @filename: the file to save the capture as
 * @error: a location for a #GError or %NULL.
 *
 * This is a convenience function for copying a capture file for which
 * you may have already discarded the writer for.
 *
 * Returns: %TRUE on success; otherwise %FALSE and @error is set.
 */
gboolean
sysprof_capture_reader_save_as (SysprofCaptureReader  *self,
                                const gchar           *filename,
                                GError               **error)
{
  struct stat stbuf;
  off_t in_off;
  gsize to_write;
  int fd = -1;

  g_assert (self != NULL);
  g_assert (filename != NULL);

  if (-1 == (fd = open (filename, O_CREAT | O_WRONLY, 0640)))
    goto handle_errno;

  if (-1 == fstat (self->fd, &stbuf))
    goto handle_errno;

  if (-1 == ftruncate (fd, stbuf.st_size))
    goto handle_errno;

  if ((off_t)-1 == lseek (fd, 0L, SEEK_SET))
    goto handle_errno;

  in_off = 0;
  to_write = stbuf.st_size;

  while (to_write > 0)
    {
      gssize written;

      written = _sysprof_sendfile (fd, self->fd, &in_off, to_write);

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
  if (fd != -1)
    close (fd);

  g_set_error (error,
               G_FILE_ERROR,
               g_file_error_from_errno (errno),
               "%s", g_strerror (errno));

  return FALSE;
}

gint64
sysprof_capture_reader_get_start_time (SysprofCaptureReader *self)
{
  g_return_val_if_fail (self != NULL, 0);

  if (self->endian != G_BYTE_ORDER)
    return GUINT64_SWAP_LE_BE (self->header.time);

  return self->header.time;
}

/**
 * sysprof_capture_reader_get_end_time:
 *
 * This function will return the end time for the capture, which is the
 * same as the last event added to the capture.
 *
 * If the end time has been stored in the capture header, that will be used.
 * Otherwise, the time is discovered from the last capture frame and therefore
 * the caller must have read all frames to ensure this value is accurate.
 *
 * Captures created by sysprof versions containing this API will have the
 * end time set if the output capture file-descriptor supports seeking.
 *
 * Since: 3.22.1
 */
gint64
sysprof_capture_reader_get_end_time (SysprofCaptureReader *self)
{
  gint64 end_time = 0;

  g_return_val_if_fail (self != NULL, 0);

  if (self->header.end_time != 0)
    {
      if (self->endian != G_BYTE_ORDER)
        end_time = GUINT64_SWAP_LE_BE (self->header.end_time);
      else
        end_time = self->header.end_time;
    }

  return MAX (self->end_time, end_time);
}

/**
 * sysprof_capture_reader_copy:
 *
 * This function makes a copy of the reader. Since readers use
 * positioned reads with pread(), this allows you to have multiple
 * readers with the shared file descriptor. This uses dup() to create
 * another file descriptor.
 *
 * Returns: (transfer full): A copy of @self with a new file-descriptor.
 */
SysprofCaptureReader *
sysprof_capture_reader_copy (SysprofCaptureReader *self)
{
  SysprofCaptureReader *copy;
  int fd;

  g_return_val_if_fail (self != NULL, NULL);

  if (-1 == (fd = dup (self->fd)))
    return NULL;

  copy = g_new0 (SysprofCaptureReader, 1);

  *copy = *self;

  copy->ref_count = 1;
  copy->filename = g_strdup (self->filename);
  copy->fd = fd;

  copy->buf = g_malloc (self->bufsz);
  memcpy (copy->buf, self->buf, self->bufsz);

  return copy;
}
