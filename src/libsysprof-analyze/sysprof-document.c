/* sysprof-document.c
 *
 * Copyright 2023 Christian Hergert <chergert@redhat.com>
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

#include <fcntl.h>

#include <glib/gstdio.h>

#include "sysprof-capture-frame-object-private.h"
#include "sysprof-document.h"

struct _SysprofDocument
{
  GObject                   parent_instance;
  GPtrArray                *frames;
  GMappedFile              *mapped_file;
  SysprofCaptureFileHeader  header;
  guint                     is_native : 1;
};

static GType
sysprof_document_get_item_type (GListModel *model)
{
  return SYSPROF_TYPE_CAPTURE_FRAME_OBJECT;
}

static guint
sysprof_document_get_n_items (GListModel *model)
{
  return SYSPROF_DOCUMENT (model)->frames->len;
}

static gpointer
sysprof_document_get_item (GListModel *model,
                                guint       position)
{
  SysprofDocument *self = SYSPROF_DOCUMENT (model);

  if (position >= self->frames->len)
    return NULL;

  return sysprof_capture_frame_object_new (self->mapped_file,
                                           g_ptr_array_index (self->frames, position),
                                           self->is_native);
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = sysprof_document_get_item_type;
  iface->get_n_items = sysprof_document_get_n_items;
  iface->get_item = sysprof_document_get_item;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofDocument, sysprof_document, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

static void
sysprof_document_finalize (GObject *object)
{
  SysprofDocument *self = (SysprofDocument *)object;

  g_clear_pointer (&self->mapped_file, g_mapped_file_unref);
  g_clear_pointer (&self->frames, g_ptr_array_unref);

  G_OBJECT_CLASS (sysprof_document_parent_class)->finalize (object);
}
static void
sysprof_document_class_init (SysprofDocumentClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_document_finalize;
}

static void
sysprof_document_init (SysprofDocument *self)
{
  self->frames = g_ptr_array_new ();
}

static gboolean
sysprof_document_load (SysprofDocument  *self,
                            int                   capture_fd,
                            GError              **error)
{
  const guint8 *data;
  goffset pos;
  gsize len;

  g_assert (SYSPROF_IS_DOCUMENT (self));
  g_assert (capture_fd > -1);

  if (!(self->mapped_file = g_mapped_file_new_from_fd (capture_fd, FALSE, error)))
    return FALSE;

  data = (const guint8 *)g_mapped_file_get_contents (self->mapped_file);
  len = g_mapped_file_get_length (self->mapped_file);

  if (len < sizeof self->header)
    return FALSE;

  /* Keep a copy of our header */
  memcpy (&self->header, data, sizeof self->header);
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  self->is_native = !!self->header.little_endian;
#else
  self->is_native = !self->header.little_endian;
#endif

  if (!self->is_native)
    {
      self->header.time = GUINT64_SWAP_LE_BE (self->header.time);
      self->header.end_time = GUINT64_SWAP_LE_BE (self->header.end_time);
    }

  pos = sizeof self->header;
  while (pos < (len - sizeof(guint16)))
    {
      guint16 frame_len;

      memcpy (&frame_len, &data[pos], sizeof frame_len);
      if (!self->is_native)
        frame_len = GUINT16_SWAP_LE_BE (self->is_native);

      if (frame_len < sizeof (SysprofCaptureFrame))
        break;

      g_ptr_array_add (self->frames, (gpointer)&data[pos]);

      pos += frame_len;
    }

  return TRUE;
}

/**
 * sysprof_document_new_from_fd:
 * @capture_fd: a file-descriptor to be mapped
 * @error: a location for a #GError, or %NULL
 *
 * Creates a new memory map using @capture_fd to read the underlying
 * Sysprof capture.
 *
 * No ownership of @capture_fd is transferred, and the caller may close
 * @capture_fd after calling this function.
 *
 * Returns: A #SysprofDocument if successful; otherwise %NULL
 *   and @error is set.
 *
 * Since: 45.0
 */
SysprofDocument *
sysprof_document_new_from_fd (int      capture_fd,
                              GError **error)
{
  g_autoptr(SysprofDocument) self = NULL;

  g_return_val_if_fail (capture_fd > -1, NULL);

  self = g_object_new (SYSPROF_TYPE_DOCUMENT, NULL);

  if (!sysprof_document_load (self, capture_fd, error))
    return NULL;

  return g_steal_pointer (&self);
}

/**
 * sysprof_document_new:
 * @filename: a path to a capture file
 * @error: location for a #GError, or %NULL
 *
 * Similar to sysprof_document_new_from_fd() but opens the file found
 * at @filename as a #GMappedFile.
 *
 * Returns: a #SysprofDocument if successful; otherwise %NULL
 *   and @error is set.
 *
 * Since: 45.0
 */
SysprofDocument *
sysprof_document_new (const char  *filename,
                      GError     **error)
{
  g_autoptr(SysprofDocument) self = NULL;
  g_autofd int capture_fd = -1;

  g_return_val_if_fail (filename != NULL, NULL);

  if (-1 == (capture_fd = g_open (filename, O_RDONLY|O_CLOEXEC, 0)))
    {
      int errsv = errno;
      g_set_error (error,
                   G_FILE_ERROR,
                   g_file_error_from_errno (errsv),
                   "%s", g_strerror (errsv));
      return NULL;
    }

  self = g_object_new (SYSPROF_TYPE_DOCUMENT, NULL);

  if (!sysprof_document_load (self, capture_fd, error))
    return NULL;

  return g_steal_pointer (&self);
}
