/* sp-capture-reader.c
 *
 * Copyright © 2016 Christian Hergert <chergert@redhat.com>
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
 */

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "capture/sp-capture-reader.h"
#include "capture/sp-capture-writer.h"

struct _SpCaptureReader
{
  volatile gint        ref_count;
  gchar               *filename;
  guint8              *buf;
  gsize                bufsz;
  gsize                len;
  gsize                pos;
  gsize                fd_off;
  int                  fd;
  gint                 endian;
  SpCaptureFileHeader  header;
  gint64               end_time;
};

#ifndef SP_DISABLE_GOBJECT
G_DEFINE_BOXED_TYPE (SpCaptureReader, sp_capture_reader,
                     sp_capture_reader_ref, sp_capture_reader_unref)
#endif

static gboolean
sp_capture_reader_read_file_header (SpCaptureReader      *self,
                                    SpCaptureFileHeader  *header,
                                    GError              **error)
{
  g_assert (self != NULL);
  g_assert (header != NULL);

  if (sizeof *header != pread (self->fd, header, sizeof *header, 0L))
    {
      g_set_error (error,
                   G_FILE_ERROR,
                   g_file_error_from_errno (errno),
                   "%s", g_strerror (errno));
      return FALSE;
    }

  if (header->magic != SP_CAPTURE_MAGIC)
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
sp_capture_reader_finalize (SpCaptureReader *self)
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
sp_capture_reader_get_time (SpCaptureReader *self)
{
  g_return_val_if_fail (self != NULL, NULL);

  return self->header.capture_time;
}

const gchar *
sp_capture_reader_get_filename (SpCaptureReader *self)
{
  g_return_val_if_fail (self != NULL, NULL);

  return self->filename;
}

/**
 * sp_capture_reader_new_from_fd:
 * @fd: an fd to take ownership from
 * @error: a location for a #GError or %NULL
 *
 * Creates a new reader using the file-descriptor.
 *
 * This is useful if you don't necessarily have access to the filename itself.
 *
 * Returns: (transfer full): an #SpCaptureReader or %NULL upon failure.
 */
SpCaptureReader *
sp_capture_reader_new_from_fd (int      fd,
                               GError **error)
{
  SpCaptureReader *self;

  g_assert (fd > -1);

  self = g_new0 (SpCaptureReader, 1);
  self->ref_count = 1;
  self->bufsz = G_MAXUSHORT * 2;
  self->buf = g_malloc (self->bufsz);
  self->len = 0;
  self->pos = 0;
  self->fd = fd;
  self->fd_off = sizeof (SpCaptureFileHeader);

  if (!sp_capture_reader_read_file_header (self, &self->header, error))
    {
      sp_capture_reader_finalize (self);
      return NULL;
    }

  if (self->header.little_endian)
    self->endian = G_LITTLE_ENDIAN;
  else
    self->endian = G_BIG_ENDIAN;

  return self;
}

SpCaptureReader *
sp_capture_reader_new (const gchar  *filename,
                       GError      **error)
{
  SpCaptureReader *self;
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

  if (NULL == (self = sp_capture_reader_new_from_fd (fd, error)))
    {
      close (fd);
      return NULL;
    }

  self->filename = g_strdup (filename);

  return self;
}

static inline void
sp_capture_reader_bswap_frame (SpCaptureReader *self,
                               SpCaptureFrame  *frame)
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
sp_capture_reader_bswap_map (SpCaptureReader *self,
                             SpCaptureMap    *map)
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
sp_capture_reader_bswap_mark (SpCaptureReader *self,
                              SpCaptureMark   *mark)
{
  g_assert (self != NULL);
  g_assert (mark != NULL);

  if (G_UNLIKELY (self->endian != G_BYTE_ORDER))
    mark->duration = GUINT64_SWAP_LE_BE (mark->duration);
}

static inline void
sp_capture_reader_bswap_jitmap (SpCaptureReader *self,
                                SpCaptureJitmap *jitmap)
{
  g_assert (self != NULL);
  g_assert (jitmap != NULL);

  if (G_UNLIKELY (self->endian != G_BYTE_ORDER))
    jitmap->n_jitmaps = GUINT64_SWAP_LE_BE (jitmap->n_jitmaps);
}

static gboolean
sp_capture_reader_ensure_space_for (SpCaptureReader *self,
                                    gsize            len)
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
          r = pread (self->fd,
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
sp_capture_reader_skip (SpCaptureReader *self)
{
  SpCaptureFrame *frame;

  g_assert (self != NULL);
  g_assert ((self->pos % SP_CAPTURE_ALIGN) == 0);

  if (!sp_capture_reader_ensure_space_for (self, sizeof (SpCaptureFrame)))
    return FALSE;

  frame = (SpCaptureFrame *)(gpointer)&self->buf[self->pos];
  sp_capture_reader_bswap_frame (self, frame);

  if (frame->len < sizeof (SpCaptureFrame))
    return FALSE;

  if (!sp_capture_reader_ensure_space_for (self, frame->len))
    return FALSE;

  frame = (SpCaptureFrame *)(gpointer)&self->buf[self->pos];

  self->pos += frame->len;

  if ((self->pos % SP_CAPTURE_ALIGN) != 0)
    return FALSE;

  return TRUE;
}

gboolean
sp_capture_reader_peek_frame (SpCaptureReader *self,
                              SpCaptureFrame  *frame)
{
  SpCaptureFrame *real_frame;

  g_assert (self != NULL);
  g_assert ((self->pos % SP_CAPTURE_ALIGN) == 0);
  g_assert (self->pos <= self->len);
  g_assert (self->pos <= self->bufsz);

  if (!sp_capture_reader_ensure_space_for (self, sizeof *real_frame))
    return FALSE;

  g_assert ((self->pos % SP_CAPTURE_ALIGN) == 0);

  real_frame = (SpCaptureFrame *)(gpointer)&self->buf[self->pos];

  *frame = *real_frame;

  sp_capture_reader_bswap_frame (self, frame);

  if (frame->time > self->end_time)
    self->end_time = frame->time;

  return TRUE;
}

gboolean
sp_capture_reader_peek_type (SpCaptureReader    *self,
                             SpCaptureFrameType *type)
{
  SpCaptureFrame frame;

  g_assert (self != NULL);
  g_assert (type != NULL);

  if (!sp_capture_reader_peek_frame (self, &frame))
    return FALSE;

  *type = frame.type;

  return TRUE;
}

static const SpCaptureFrame *
sp_capture_reader_read_basic (SpCaptureReader    *self,
                              SpCaptureFrameType  type,
                              gsize               extra)
{
  SpCaptureFrame *frame;
  gsize len = sizeof *frame + extra;

  g_assert (self != NULL);
  g_assert ((self->pos % SP_CAPTURE_ALIGN) == 0);
  g_assert (self->pos <= self->bufsz);

  if (!sp_capture_reader_ensure_space_for (self, len))
    return NULL;

  frame = (SpCaptureFrame *)(gpointer)&self->buf[self->pos];

  sp_capture_reader_bswap_frame (self, frame);

  if (frame->len < len)
    return NULL;

  if (frame->type != type)
    return NULL;

  self->pos += frame->len;

  return frame;
}

const SpCaptureTimestamp *
sp_capture_reader_read_timestamp (SpCaptureReader *self)
{
  return (SpCaptureTimestamp *)
    sp_capture_reader_read_basic (self, SP_CAPTURE_FRAME_TIMESTAMP, 0);
}

const SpCaptureExit *
sp_capture_reader_read_exit (SpCaptureReader *self)
{
  return (SpCaptureExit *)
    sp_capture_reader_read_basic (self, SP_CAPTURE_FRAME_EXIT, 0);
}

const SpCaptureFork *
sp_capture_reader_read_fork (SpCaptureReader *self)
{
  SpCaptureFork *fk;

  g_assert (self != NULL);

  fk = (SpCaptureFork *)
    sp_capture_reader_read_basic (self, SP_CAPTURE_FRAME_FORK, sizeof(guint32));

  if (fk != NULL)
    {
      if (G_UNLIKELY (self->endian != G_BYTE_ORDER))
        fk->child_pid = GUINT32_SWAP_LE_BE (fk->child_pid);
    }

  return fk;
}

const SpCaptureMap *
sp_capture_reader_read_map (SpCaptureReader *self)
{
  SpCaptureMap *map;

  g_assert (self != NULL);
  g_assert ((self->pos % SP_CAPTURE_ALIGN) == 0);
  g_assert (self->pos <= self->bufsz);

  if (!sp_capture_reader_ensure_space_for (self, sizeof *map))
    return NULL;

  map = (SpCaptureMap *)(gpointer)&self->buf[self->pos];

  sp_capture_reader_bswap_frame (self, &map->frame);

  if (map->frame.type != SP_CAPTURE_FRAME_MAP)
    return NULL;

  if (map->frame.len < (sizeof *map + 1))
    return NULL;

  if (!sp_capture_reader_ensure_space_for (self, map->frame.len))
    return NULL;

  map = (SpCaptureMap *)(gpointer)&self->buf[self->pos];

  if (self->buf[self->pos + map->frame.len - 1] != '\0')
    return NULL;

  sp_capture_reader_bswap_map (self, map);

  self->pos += map->frame.len;

  if ((self->pos % SP_CAPTURE_ALIGN) != 0)
    return NULL;

  return map;
}

const SpCaptureMark *
sp_capture_reader_read_mark (SpCaptureReader *self)
{
  SpCaptureMark *mark;

  g_assert (self != NULL);
  g_assert ((self->pos % SP_CAPTURE_ALIGN) == 0);
  g_assert (self->pos <= self->bufsz);

  if (!sp_capture_reader_ensure_space_for (self, sizeof *mark))
    return NULL;

  mark = (SpCaptureMark *)(gpointer)&self->buf[self->pos];

  sp_capture_reader_bswap_frame (self, &mark->frame);

  if (mark->frame.type != SP_CAPTURE_FRAME_MARK)
    return NULL;

  if (mark->frame.len < (sizeof *mark + 1))
    return NULL;

  if (!sp_capture_reader_ensure_space_for (self, mark->frame.len))
    return NULL;

  mark = (SpCaptureMark *)(gpointer)&self->buf[self->pos];

  sp_capture_reader_bswap_mark (self, mark);

  self->pos += mark->frame.len;

  if ((self->pos % SP_CAPTURE_ALIGN) != 0)
    return NULL;

  /* Ensure trailing \0 in name and message */
  mark->name[sizeof mark->name - 1] = 0;
  self->buf[self->pos + mark->frame.len - 1] = 0;

  return mark;
}

const SpCaptureProcess *
sp_capture_reader_read_process (SpCaptureReader *self)
{
  SpCaptureProcess *process;

  g_assert (self != NULL);
  g_assert ((self->pos % SP_CAPTURE_ALIGN) == 0);
  g_assert (self->pos <= self->bufsz);

  if (!sp_capture_reader_ensure_space_for (self, sizeof *process))
    return NULL;

  process = (SpCaptureProcess *)(gpointer)&self->buf[self->pos];

  sp_capture_reader_bswap_frame (self, &process->frame);

  if (process->frame.type != SP_CAPTURE_FRAME_PROCESS)
    return NULL;

  if (process->frame.len < (sizeof *process + 1))
    return NULL;

  if (!sp_capture_reader_ensure_space_for (self, process->frame.len))
    return NULL;

  process = (SpCaptureProcess *)(gpointer)&self->buf[self->pos];

  if (self->buf[self->pos + process->frame.len - 1] != '\0')
    return NULL;

  self->pos += process->frame.len;

  if ((self->pos % SP_CAPTURE_ALIGN) != 0)
    return NULL;

  return process;
}

GHashTable *
sp_capture_reader_read_jitmap (SpCaptureReader *self)
{
  g_autoptr(GHashTable) ret = NULL;
  SpCaptureJitmap *jitmap;
  guint8 *buf;
  guint8 *endptr;
  guint i;

  g_assert (self != NULL);
  g_assert ((self->pos % SP_CAPTURE_ALIGN) == 0);
  g_assert (self->pos <= self->bufsz);

  if (!sp_capture_reader_ensure_space_for (self, sizeof *jitmap))
    return NULL;

  jitmap = (SpCaptureJitmap *)(gpointer)&self->buf[self->pos];

  sp_capture_reader_bswap_frame (self, &jitmap->frame);

  if (jitmap->frame.type != SP_CAPTURE_FRAME_JITMAP)
    return NULL;

  if (jitmap->frame.len < sizeof *jitmap)
    return NULL;

  if (!sp_capture_reader_ensure_space_for (self, jitmap->frame.len))
    return NULL;

  jitmap = (SpCaptureJitmap *)(gpointer)&self->buf[self->pos];

  ret = g_hash_table_new_full (NULL, NULL, NULL, g_free);

  buf = jitmap->data;
  endptr = &self->buf[self->pos + jitmap->frame.len];

  for (i = 0; i < jitmap->n_jitmaps; i++)
    {
      SpCaptureAddress addr;
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

  sp_capture_reader_bswap_jitmap (self, jitmap);

  self->pos += jitmap->frame.len;

  return g_steal_pointer (&ret);
}

const SpCaptureSample *
sp_capture_reader_read_sample (SpCaptureReader *self)
{
  SpCaptureSample *sample;

  g_assert (self != NULL);
  g_assert ((self->pos % SP_CAPTURE_ALIGN) == 0);
  g_assert (self->pos <= self->bufsz);

  if (!sp_capture_reader_ensure_space_for (self, sizeof *sample))
    return NULL;

  sample = (SpCaptureSample *)(gpointer)&self->buf[self->pos];

  sp_capture_reader_bswap_frame (self, &sample->frame);

  if (sample->frame.type != SP_CAPTURE_FRAME_SAMPLE)
    return NULL;

  if (sample->frame.len < sizeof *sample)
    return NULL;

  if (self->endian != G_BYTE_ORDER)
    sample->n_addrs = GUINT16_SWAP_LE_BE (sample->n_addrs);

  if (sample->frame.len < (sizeof *sample + (sizeof(SpCaptureAddress) * sample->n_addrs)))
    return NULL;

  if (!sp_capture_reader_ensure_space_for (self, sample->frame.len))
    return NULL;

  sample = (SpCaptureSample *)(gpointer)&self->buf[self->pos];

  if (G_UNLIKELY (self->endian != G_BYTE_ORDER))
    {
      guint i;

      for (i = 0; i < sample->n_addrs; i++)
        sample->addrs[i] = GUINT64_SWAP_LE_BE (sample->addrs[i]);
    }

  self->pos += sample->frame.len;

  return sample;
}

const SpCaptureFrameCounterDefine *
sp_capture_reader_read_counter_define (SpCaptureReader *self)
{
  SpCaptureFrameCounterDefine *def;

  g_assert (self != NULL);
  g_assert ((self->pos % SP_CAPTURE_ALIGN) == 0);
  g_assert (self->pos <= self->bufsz);

  if (!sp_capture_reader_ensure_space_for (self, sizeof *def))
    return NULL;

  def = (SpCaptureFrameCounterDefine *)(gpointer)&self->buf[self->pos];

  if (def->frame.type != SP_CAPTURE_FRAME_CTRDEF)
    return NULL;

  if (def->frame.len < sizeof *def)
    return NULL;

  if (G_UNLIKELY (self->endian != G_BYTE_ORDER))
    def->n_counters = GUINT16_SWAP_LE_BE (def->n_counters);

  if (def->frame.len < (sizeof *def + (sizeof (SpCaptureFrameCounterDefine) * def->n_counters)))
    return NULL;

  if (!sp_capture_reader_ensure_space_for (self, def->frame.len))
    return NULL;

  def = (SpCaptureFrameCounterDefine *)(gpointer)&self->buf[self->pos];

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

const SpCaptureFrameCounterSet *
sp_capture_reader_read_counter_set (SpCaptureReader *self)
{
  SpCaptureFrameCounterSet *set;

  g_assert (self != NULL);
  g_assert ((self->pos % SP_CAPTURE_ALIGN) == 0);
  g_assert (self->pos <= self->bufsz);

  if (!sp_capture_reader_ensure_space_for (self, sizeof *set))
    return NULL;

  set = (SpCaptureFrameCounterSet *)(gpointer)&self->buf[self->pos];

  if (set->frame.type != SP_CAPTURE_FRAME_CTRSET)
    return NULL;

  if (set->frame.len < sizeof *set)
    return NULL;

  if (self->endian != G_BYTE_ORDER)
    set->n_values = GUINT16_SWAP_LE_BE (set->n_values);

  if (set->frame.len < (sizeof *set + (sizeof (SpCaptureCounterValues) * set->n_values)))
    return NULL;

  if (!sp_capture_reader_ensure_space_for (self, set->frame.len))
    return NULL;

  set = (SpCaptureFrameCounterSet *)(gpointer)&self->buf[self->pos];

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
sp_capture_reader_reset (SpCaptureReader *self)
{
  g_assert (self != NULL);

  self->fd_off = sizeof (SpCaptureFileHeader);
  self->pos = 0;
  self->len = 0;

  return TRUE;
}

SpCaptureReader *
sp_capture_reader_ref (SpCaptureReader *self)
{
  g_assert (self != NULL);
  g_assert (self->ref_count > 0);

  g_atomic_int_inc (&self->ref_count);

  return self;
}

void
sp_capture_reader_unref (SpCaptureReader *self)
{
  g_assert (self != NULL);
  g_assert (self->ref_count > 0);

  if (g_atomic_int_dec_and_test (&self->ref_count))
    sp_capture_reader_finalize (self);
}

gboolean
sp_capture_reader_splice (SpCaptureReader  *self,
                          SpCaptureWriter  *dest,
                          GError          **error)
{
  g_assert (self != NULL);
  g_assert (self->fd != -1);
  g_assert (dest != NULL);

  /* Flush before writing anything to ensure consistency */
  if (!sp_capture_writer_flush (dest))
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
  return _sp_capture_writer_splice_from_fd (dest, self->fd, error);
}

/**
 * sp_capture_reader_save_as:
 * @self: An #SpCaptureReader
 * @filename: the file to save the capture as
 * @error: a location for a #GError or %NULL.
 *
 * This is a convenience function for copying a capture file for which
 * you may have already discarded the writer for.
 *
 * Returns: %TRUE on success; otherwise %FALSE and @error is set.
 */
gboolean
sp_capture_reader_save_as (SpCaptureReader  *self,
                           const gchar      *filename,
                           GError          **error)
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

      written = sendfile (fd, self->fd, &in_off, to_write);

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
sp_capture_reader_get_start_time (SpCaptureReader *self)
{
  g_return_val_if_fail (self != NULL, 0);

  if (self->endian != G_BYTE_ORDER)
    return GUINT64_SWAP_LE_BE (self->header.time);

  return self->header.time;
}

/**
 * sp_capture_reader_get_end_time:
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
sp_capture_reader_get_end_time (SpCaptureReader *self)
{
  g_return_val_if_fail (self != NULL, 0);

  if (self->header.end_time != 0)
    {
      if (self->endian != G_BYTE_ORDER)
        return GUINT64_SWAP_LE_BE (self->header.end_time);
      return self->header.end_time;
    }

  return self->end_time;
}

/**
 * sp_capture_reader_copy:
 *
 * This function makes a copy of the reader. Since readers use
 * positioned reads with pread(), this allows you to have multiple
 * readers with the shared file descriptor. This uses dup() to create
 * another file descriptor.
 *
 * Returns: (transfer full): A copy of @self with a new file-descriptor.
 */
SpCaptureReader *
sp_capture_reader_copy (SpCaptureReader *self)
{
  SpCaptureReader *copy;
  int fd;

  g_return_val_if_fail (self != NULL, NULL);

  if (-1 == (fd = dup (self->fd)))
    return NULL;

  copy = g_new0 (SpCaptureReader, 1);

  *copy = *self;

  copy->ref_count = 1;
  copy->filename = g_strdup (self->filename);
  copy->fd = fd;

  copy->buf = g_malloc (self->bufsz);
  memcpy (copy->buf, self->buf, self->bufsz);

  return copy;
}
