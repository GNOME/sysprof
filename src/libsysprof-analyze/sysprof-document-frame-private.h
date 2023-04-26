/* sysprof-document-frame-private.h
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

#pragma once

#include <sysprof-capture.h>

#include "sysprof-document-frame.h"

G_BEGIN_DECLS

struct _SysprofDocumentFrame
{
  GObject                    parent_instance;
  GMappedFile               *mapped_file;
  const SysprofCaptureFrame *frame;
  guint                      needs_swap : 1;
};

struct _SysprofDocumentFrameClass
{
  GObjectClass parent_class;
};

SysprofDocumentFrame *_sysprof_document_frame_new (GMappedFile               *mapped,
                                                   const SysprofCaptureFrame *frame,
                                                   gboolean                   needs_swap);

#define SYSPROF_DOCUMENT_FRAME_GET(obj, type) \
  ((const type *)(SYSPROF_DOCUMENT_FRAME(obj)->frame))

#define SYSPROF_DOCUMENT_FRAME_NEEDS_SWAP(obj) \
  (SYSPROF_DOCUMENT_FRAME (obj)->needs_swap)

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
# define SYSPROF_DOCUMENT_FRAME_UINT16(obj, val) \
   (SYSPROF_DOCUMENT_FRAME_NEEDS_SWAP(obj) ? GUINT16_TO_LE(val) : (val))
# define SYSPROF_DOCUMENT_FRAME_INT16(obj, val) \
   (SYSPROF_DOCUMENT_FRAME_NEEDS_SWAP(obj) ? GINT16_TO_LE(val) : (val))
# define SYSPROF_DOCUMENT_FRAME_UINT32(obj, val) \
   (SYSPROF_DOCUMENT_FRAME_NEEDS_SWAP(obj) ? GUINT32_TO_LE(val) : (val))
# define SYSPROF_DOCUMENT_FRAME_INT32(obj, val) \
   (SYSPROF_DOCUMENT_FRAME_NEEDS_SWAP(obj) ? GINT32_TO_LE(val) : (val))
# define SYSPROF_DOCUMENT_FRAME_UINT64(obj, val) \
   (SYSPROF_DOCUMENT_FRAME_NEEDS_SWAP(obj) ? GUINT64_TO_LE(val) : (val))
# define SYSPROF_DOCUMENT_FRAME_INT64(obj, val) \
   (SYSPROF_DOCUMENT_FRAME_NEEDS_SWAP(obj) ? GINT64_TO_LE(val) : (val))
#else
# define SYSPROF_DOCUMENT_FRAME_UINT16(obj, val) \
   (SYSPROF_DOCUMENT_FRAME_NEEDS_SWAP(obj) ? GUINT16_TO_BE(val) : (val))
# define SYSPROF_DOCUMENT_FRAME_INT16(obj, val) \
   (SYSPROF_DOCUMENT_FRAME_NEEDS_SWAP(obj) ? GINT16_TO_BE(val) : (val))
# define SYSPROF_DOCUMENT_FRAME_UINT32(obj, val) \
   (SYSPROF_DOCUMENT_FRAME_NEEDS_SWAP(obj) ? GUINT32_TO_BE(val) : (val))
# define SYSPROF_DOCUMENT_FRAME_INT32(obj, val) \
   (SYSPROF_DOCUMENT_FRAME_NEEDS_SWAP(obj) ? GINT32_TO_BE(val) : (val))
# define SYSPROF_DOCUMENT_FRAME_UINT64(obj, val) \
   (SYSPROF_DOCUMENT_FRAME_NEEDS_SWAP(obj) ? GUINT64_TO_BE(val) : (val))
# define SYSPROF_DOCUMENT_FRAME_INT64(obj, val) \
   (SYSPROF_DOCUMENT_FRAME_NEEDS_SWAP(obj) ? GINT64_TO_BE(val) : (val))
#endif

G_END_DECLS
