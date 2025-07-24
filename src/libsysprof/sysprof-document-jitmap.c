/* sysprof-document-jitmap.c
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

#include "sysprof-document-frame-private.h"
#include "sysprof-document-jitmap.h"

typedef struct _Jitmap
{
  SysprofAddress  address;
  const char     *name;
} Jitmap;

struct _SysprofDocumentJitmap
{
  SysprofDocumentFrame parent_instance;
  GArray *jitmaps;
  guint initialized : 1;
};

struct _SysprofDocumentJitmapClass
{
  SysprofDocumentFrameClass parent_class;
};

enum {
  PROP_0,
  PROP_SIZE,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofDocumentJitmap, sysprof_document_jitmap, SYSPROF_TYPE_DOCUMENT_FRAME)

static GParamSpec *properties [N_PROPS];

static void
sysprof_document_jitmap_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  SysprofDocumentJitmap *self = SYSPROF_DOCUMENT_JITMAP (object);

  switch (prop_id)
    {
    case PROP_SIZE:
      g_value_set_uint (value, sysprof_document_jitmap_get_size (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_document_jitmap_class_init (SysprofDocumentJitmapClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = sysprof_document_jitmap_get_property;

  properties [PROP_SIZE] =
    g_param_spec_string ("size", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_document_jitmap_init (SysprofDocumentJitmap *self)
{
  self->jitmaps = g_array_new (FALSE, FALSE, sizeof (Jitmap));
}

guint
sysprof_document_jitmap_get_size (SysprofDocumentJitmap *self)
{
  const SysprofCaptureJitmap *jitmap;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_JITMAP (self), 0);

  jitmap = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureJitmap);

  return SYSPROF_DOCUMENT_FRAME_UINT32 (self, jitmap->n_jitmaps);
}

/**
 * sysprof_document_jitmap_get_mapping:
 * @self: a #SysprofDocumentJitmap
 * @position: the index of the mapping
 * @address: (out): a location for the address
 *
 * Gets the @position mapping and returns it as @address and the return value
 * of this function.
 *
 * Returns: (nullable): the name of the symbol, or %NULL if @position is
 *   out of bounds.
 */
const char *
sysprof_document_jitmap_get_mapping (SysprofDocumentJitmap *self,
                                     guint                  nth,
                                     SysprofAddress        *address)
{
  const SysprofCaptureJitmap *frame;
  const Jitmap *j;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_JITMAP (self), NULL);
  g_return_val_if_fail (address != NULL, NULL);

  if G_UNLIKELY (!self->initialized)
    {
      const guint8 *pos;
      const guint8 *endptr;

      self->initialized = TRUE;

      frame = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureJitmap);
      endptr = SYSPROF_DOCUMENT_FRAME_ENDPTR (self);
      pos = frame->data;

      while (pos < endptr)
        {
          SysprofAddress addr;
          Jitmap map;

          if (pos + sizeof addr >= endptr)
            break;

          memcpy (&addr, pos, sizeof addr);
          pos += sizeof addr;

          map.address = SYSPROF_DOCUMENT_FRAME_UINT64 (self, addr);

          if (!(map.name = SYSPROF_DOCUMENT_FRAME_CSTRING (self, (const char *)pos)))
            break;

          pos += strlen (map.name) + 1;

          g_array_append_val (self->jitmaps, map);
        }
    }

  if (nth >= self->jitmaps->len)
    return NULL;

  j = &g_array_index (self->jitmaps, Jitmap, nth);
  *address = j->address;
  return j->name;
}

