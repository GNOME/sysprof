/* sysprof-capture-frame-object.c
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

struct _SysprofCaptureFrameObject
{
  GObject parent_instance;
  GMappedFile *mapped_file;
  const SysprofCaptureFrame *frame;
  guint is_native : 1;
};

G_DEFINE_FINAL_TYPE (SysprofCaptureFrameObject, sysprof_capture_frame_object, G_TYPE_OBJECT)

static void
sysprof_capture_frame_object_finalize (GObject *object)
{
  SysprofCaptureFrameObject *self = (SysprofCaptureFrameObject *)object;

  g_clear_pointer (&self->mapped_file, g_mapped_file_unref);
  self->frame = NULL;

  G_OBJECT_CLASS (sysprof_capture_frame_object_parent_class)->finalize (object);
}

static void
sysprof_capture_frame_object_class_init (SysprofCaptureFrameObjectClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_capture_frame_object_finalize;
}

static void
sysprof_capture_frame_object_init (SysprofCaptureFrameObject *self)
{
}

SysprofCaptureFrameObject *
sysprof_capture_frame_object_new (GMappedFile   *mapped_file,
                                  gconstpointer  data,
                                  gboolean       is_native)
{
  SysprofCaptureFrameObject *self;

  self = g_object_new (SYSPROF_TYPE_CAPTURE_FRAME_OBJECT, NULL);
  self->mapped_file = g_mapped_file_ref (mapped_file);
  self->frame = data;
  self->is_native = !!is_native;

  return self;
}
