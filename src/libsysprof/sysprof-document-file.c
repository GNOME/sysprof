/* sysprof-document-file.c
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

#include "sysprof-document-file-chunk.h"
#include "sysprof-document-file-private.h"
#include "sysprof-document-frame-private.h"

struct _SysprofDocumentFile
{
  GObject    parent_instance;
  char      *path;
  GPtrArray *file_chunks;
  guint      compressed : 1;
};

enum {
  PROP_0,
  PROP_BYTES,
  PROP_IS_COMPRESSED,
  PROP_PATH,
  PROP_SIZE,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofDocumentFile, sysprof_document_file, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static void
sysprof_document_file_finalize (GObject *object)
{
  SysprofDocumentFile *self = (SysprofDocumentFile *)object;

  g_clear_pointer (&self->path, g_free);
  g_clear_pointer (&self->file_chunks, g_ptr_array_unref);

  G_OBJECT_CLASS (sysprof_document_file_parent_class)->finalize (object);
}

static void
sysprof_document_file_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  SysprofDocumentFile *self = SYSPROF_DOCUMENT_FILE (object);

  switch (prop_id)
    {
    case PROP_PATH:
      g_value_set_string (value, sysprof_document_file_get_path (self));
      break;

    case PROP_BYTES:
      g_value_take_boxed (value, sysprof_document_file_dup_bytes (self));
      break;

    case PROP_IS_COMPRESSED:
      g_value_set_boolean (value, sysprof_document_file_is_compressed (self));
      break;

    case PROP_SIZE:
      g_value_set_uint64 (value, sysprof_document_file_get_size (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_document_file_class_init (SysprofDocumentFileClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_document_file_finalize;
  object_class->get_property = sysprof_document_file_get_property;

  properties [PROP_PATH] =
    g_param_spec_string ("path", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_BYTES] =
    g_param_spec_boxed ("bytes", NULL, NULL,
                        G_TYPE_BYTES,
                        (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_IS_COMPRESSED] =
    g_param_spec_boolean ("is-compressed", NULL, NULL,
                          FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SIZE] =
    g_param_spec_uint64 ("size", NULL, NULL,
                         0, G_MAXUINT64, 0,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_document_file_init (SysprofDocumentFile *self)
{
}

SysprofDocumentFile *
_sysprof_document_file_new (const char *path,
                            GPtrArray  *file_chunks,
                            gboolean    compressed)
{
  SysprofDocumentFile *self;

  g_return_val_if_fail (path != NULL, NULL);
  g_return_val_if_fail (file_chunks != NULL, NULL);

  self = g_object_new (SYSPROF_TYPE_DOCUMENT_FILE, NULL);
  self->path = g_strdup (path);
  self->file_chunks = file_chunks;
  self->compressed = !!compressed;

  return self;
}

const char *
sysprof_document_file_get_path (SysprofDocumentFile *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_FILE (self), NULL);

  return self->path;
}

/**
 * sysprof_document_file_dup_contents:
 * @self: a #SysprofDocumentFile
 *
 * Creates a new #GBytes containing the contents of the file.
 *
 * Returns: (transfer full): a #GBytes
 */
GBytes *
sysprof_document_file_dup_bytes (SysprofDocumentFile *self)
{
  static const char zero = 0;
  GArray *ar;
  guint len;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_FILE (self), NULL);

  ar = g_array_new (TRUE, FALSE, sizeof (char));

  for (guint i = 0; i < self->file_chunks->len; i++)
    {
      SysprofDocumentFileChunk *file_chunk = g_ptr_array_index (self->file_chunks, i);
      const guint8 *data = sysprof_document_file_chunk_get_data (file_chunk, &len);

      g_array_append_vals (ar, data, len);
    }

  len = ar->len;

  if (self->compressed)
    {
      guint8 *data = (guint8 *)g_array_free (ar, FALSE);
      g_autoptr(GInputStream) input = g_memory_input_stream_new_from_data (data, len, g_free);
      g_autoptr(GOutputStream) memory_output = g_memory_output_stream_new_resizable ();
      g_autoptr(GZlibDecompressor) zlib = g_zlib_decompressor_new (G_ZLIB_COMPRESSOR_FORMAT_GZIP);
      g_autoptr(GOutputStream) zlib_output = g_converter_output_stream_new (memory_output, G_CONVERTER (zlib));
      g_autoptr(GBytes) bytes = NULL;

      g_output_stream_splice (zlib_output,
                              input,
                              G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE,
                              NULL, NULL);
      g_output_stream_write (memory_output, &zero, 1, NULL, NULL);
      g_output_stream_close (memory_output, NULL, NULL);
      bytes = g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (memory_output));

      g_assert (bytes != NULL);
      g_assert (g_bytes_get_size (bytes) > 0);
      g_assert (((char *)g_bytes_get_data (bytes, NULL))[g_bytes_get_size (bytes)-1] == 0);

      return g_bytes_new_from_bytes (bytes, 0, g_bytes_get_size (bytes) - 1);
    }

  g_array_append_val (ar, zero);
  g_assert (ar->data[len] == 0);

  return g_bytes_new_take (g_array_free (ar, FALSE), len);
}

/**
 * sysprof_document_file_read:
 * @self: a #SysprofDocumentFile
 *
 * Creates a new input stream containing the contents of the file
 * within the document.
 *
 * Returns: (transfer full): a #GInputstream
 */
GInputStream *
sysprof_document_file_read (SysprofDocumentFile *self)
{
  g_autoptr(GInputStream) input = NULL;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_FILE (self), NULL);

  input = g_memory_input_stream_new ();

  for (guint i = 0; i < self->file_chunks->len; i++)
    {
      g_autoptr(GBytes) bytes = NULL;
      SysprofDocumentFileChunk *file_chunk;
      const guint8 *data;
      guint len;

      file_chunk = g_ptr_array_index (self->file_chunks, i);
      data = sysprof_document_file_chunk_get_data (file_chunk, &len);

      bytes = g_bytes_new_with_free_func (data,
                                          len,
                                          (GDestroyNotify)g_mapped_file_unref,
                                          g_mapped_file_ref (SYSPROF_DOCUMENT_FRAME (file_chunk)->mapped_file));

      g_memory_input_stream_add_bytes (G_MEMORY_INPUT_STREAM (input), bytes);
    }

  if (self->compressed)
    {
      g_autoptr(GZlibDecompressor) zlib = g_zlib_decompressor_new (G_ZLIB_COMPRESSOR_FORMAT_GZIP);

      return g_converter_input_stream_new (input, G_CONVERTER (zlib));
    }

  return g_steal_pointer (&input);
}

gsize
sysprof_document_file_get_size (SysprofDocumentFile *self)
{
  gsize size = 0;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_FILE (self), 0);

  for (guint i = 0; i < self->file_chunks->len; i++)
    {
      SysprofDocumentFileChunk *file_chunk = g_ptr_array_index (self->file_chunks, i);

      size += sysprof_document_file_chunk_get_size (file_chunk);
    }

  return size;
}

/**
 * sysprof_document_file_is_compressed:
 * @self: a #SysprofDocumentFile
 *
 * Checks if the embedded file is compressed. If so, %TRUE is returned.
 *
 * Note that files are transparently decompressed when opening streams.
 *
 * Returns: %TRUE if the embedded file was compressed
 */
gboolean
sysprof_document_file_is_compressed (SysprofDocumentFile *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_FILE (self), FALSE);

  return self->compressed;
}
