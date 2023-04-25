/* sysprof-capture-model.c
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

#include "sysprof-capture-frame-object-private.h"
#include "sysprof-capture-model.h"

struct _SysprofCaptureModel
{
  GObject                   parent_instance;
  GPtrArray                *frames;
  GMappedFile              *mapped_file;
  SysprofCaptureFileHeader  header;
  guint                     is_native : 1;
};

static GType
sysprof_capture_model_get_item_type (GListModel *model)
{
  return SYSPROF_TYPE_CAPTURE_FRAME_OBJECT;
}

static guint
sysprof_capture_model_get_n_items (GListModel *model)
{
  return SYSPROF_CAPTURE_MODEL (model)->frames->len;
}

static gpointer
sysprof_capture_model_get_item (GListModel *model,
                                guint       position)
{
  SysprofCaptureModel *self = SYSPROF_CAPTURE_MODEL (model);

  if (position >= self->frames->len)
    return NULL;

  return sysprof_capture_frame_object_new (self->mapped_file,
                                           g_ptr_array_index (self->frames, position),
                                           self->is_native);
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = sysprof_capture_model_get_item_type;
  iface->get_n_items = sysprof_capture_model_get_n_items;
  iface->get_item = sysprof_capture_model_get_item;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofCaptureModel, sysprof_capture_model, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

static void
sysprof_capture_model_finalize (GObject *object)
{
  SysprofCaptureModel *self = (SysprofCaptureModel *)object;

  g_clear_pointer (&self->mapped_file, g_mapped_file_unref);
  g_clear_pointer (&self->frames, g_ptr_array_unref);

  G_OBJECT_CLASS (sysprof_capture_model_parent_class)->finalize (object);
}
static void
sysprof_capture_model_class_init (SysprofCaptureModelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_capture_model_finalize;
}

static void
sysprof_capture_model_init (SysprofCaptureModel *self)
{
  self->frames = g_ptr_array_new ();
}

static gboolean
sysprof_capture_model_load (SysprofCaptureModel  *self,
                            int                   capture_fd,
                            GError              **error)
{
  const guint8 *data;
  goffset pos;
  gsize len;

  g_assert (SYSPROF_IS_CAPTURE_MODEL (self));
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

SysprofCaptureModel *
sysprof_capture_model_new_from_fd (int      capture_fd,
                                   GError **error)
{
  g_autoptr(SysprofCaptureModel) self = NULL;

  g_return_val_if_fail (capture_fd > -1, NULL);

  self = g_object_new (SYSPROF_TYPE_CAPTURE_MODEL, NULL);

  if (!sysprof_capture_model_load (self, capture_fd, error))
    return NULL;

  return g_steal_pointer (&self);
}
