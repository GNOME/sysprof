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

#include "sysprof-document-private.h"

#include "sysprof-document-bitset-index-private.h"
#include "sysprof-document-file-chunk.h"
#include "sysprof-document-file-private.h"
#include "sysprof-document-frame-private.h"
#include "sysprof-document-symbols-private.h"
#include "sysprof-symbolizer-private.h"

struct _SysprofDocument
{
  GObject                   parent_instance;

  GArray                   *frames;
  GMappedFile              *mapped_file;
  const guint8             *base;

  GtkBitset                *file_chunks;
  GtkBitset                *traceables;
  GtkBitset                *processes;

  GHashTable               *files_first_position;

  GMutex                    strings_mutex;
  GHashTable               *strings;

  SysprofCaptureFileHeader  header;
  guint                     needs_swap : 1;
};

typedef struct _SysprofDocumentFramePointer
{
  guint64 offset : 48;
  guint64 length : 16;
} SysprofDocumentFramePointer;

static GType
sysprof_document_get_item_type (GListModel *model)
{
  return SYSPROF_TYPE_DOCUMENT_FRAME;
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
  SysprofDocumentFramePointer *ptr;

  if (position >= self->frames->len)
    return NULL;

  ptr = &g_array_index (self->frames, SysprofDocumentFramePointer, position);

  return _sysprof_document_frame_new (self->mapped_file,
                                      (gconstpointer)&self->base[ptr->offset],
                                      ptr->length,
                                      self->needs_swap);
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

GtkBitset *
_sysprof_document_traceables (SysprofDocument *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  return self->traceables;
}

static void
sysprof_document_finalize (GObject *object)
{
  SysprofDocument *self = (SysprofDocument *)object;

  g_clear_pointer (&self->mapped_file, g_mapped_file_unref);
  g_clear_pointer (&self->frames, g_array_unref);
  g_clear_pointer (&self->strings, g_hash_table_unref);
  g_clear_pointer (&self->traceables, gtk_bitset_unref);
  g_clear_pointer (&self->processes, gtk_bitset_unref);
  g_clear_pointer (&self->file_chunks, gtk_bitset_unref);
  g_clear_pointer (&self->files_first_position, g_hash_table_unref);

  g_mutex_clear (&self->strings_mutex);

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
  g_mutex_init (&self->strings_mutex);

  self->frames = g_array_new (FALSE, FALSE, sizeof (SysprofDocumentFramePointer));
  self->strings = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
                                         (GDestroyNotify)g_ref_string_release);
  self->traceables = gtk_bitset_new_empty ();
  self->file_chunks = gtk_bitset_new_empty ();
  self->processes = gtk_bitset_new_empty ();

  self->files_first_position = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

static inline gboolean
has_null_byte (const char *str,
               const char *endptr)
{
  for (const char *c = str; c < endptr; c++)
    {
      if (*c == '\0')
        return TRUE;
    }

  return FALSE;
}

static gboolean
sysprof_document_load (SysprofDocument  *self,
                       int               capture_fd,
                       GError          **error)
{
  g_autoptr(GHashTable) files = NULL;
  goffset pos;
  gsize len;

  g_assert (SYSPROF_IS_DOCUMENT (self));
  g_assert (capture_fd > -1);

  if (!(self->mapped_file = g_mapped_file_new_from_fd (capture_fd, FALSE, error)))
    return FALSE;

  self->base = (const guint8 *)g_mapped_file_get_contents (self->mapped_file);
  len = g_mapped_file_get_length (self->mapped_file);

  if (len < sizeof self->header)
    return FALSE;

  /* Keep a copy of our header */
  memcpy (&self->header, self->base, sizeof self->header);
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  self->needs_swap = !self->header.little_endian;
#else
  self->needs_swap = !!self->header.little_endian;
#endif

  if (self->needs_swap)
    {
      self->header.time = GUINT64_SWAP_LE_BE (self->header.time);
      self->header.end_time = GUINT64_SWAP_LE_BE (self->header.end_time);
    }

  pos = sizeof self->header;
  while (pos < (len - sizeof(guint16)))
    {
      const SysprofCaptureFrame *tainted;
      SysprofDocumentFramePointer ptr;
      guint16 frame_len;

      memcpy (&frame_len, &self->base[pos], sizeof frame_len);
      if (self->needs_swap)
        frame_len = GUINT16_SWAP_LE_BE (frame_len);

      if (frame_len < sizeof (SysprofCaptureFrame))
        break;

      ptr.offset = pos;
      ptr.length = frame_len;

      tainted = (const SysprofCaptureFrame *)(gpointer)&self->base[pos];

      if (tainted->type == SYSPROF_CAPTURE_FRAME_SAMPLE ||
          tainted->type == SYSPROF_CAPTURE_FRAME_ALLOCATION)
        gtk_bitset_add (self->traceables, self->frames->len);
      else if (tainted->type == SYSPROF_CAPTURE_FRAME_PROCESS)
        gtk_bitset_add (self->processes, self->frames->len);
      else if (tainted->type == SYSPROF_CAPTURE_FRAME_FILE_CHUNK)
        gtk_bitset_add (self->file_chunks, self->frames->len);

      if (tainted->type == SYSPROF_CAPTURE_FRAME_FILE_CHUNK)
        {
          const SysprofCaptureFileChunk *file_chunk = (const SysprofCaptureFileChunk *)tainted;

          if (has_null_byte (file_chunk->path, (const char *)file_chunk->data) &&
              !g_hash_table_contains (self->files_first_position, file_chunk->path))
            g_hash_table_insert (self->files_first_position,
                                 g_strdup (file_chunk->path),
                                 GUINT_TO_POINTER (self->frames->len));
        }

      pos += frame_len;

      g_array_append_val (self->frames, ptr);
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

char *
_sysprof_document_ref_string (SysprofDocument *self,
                              const char      *name)
{
  char *ret;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  if (name == NULL)
    return NULL;

  g_mutex_lock (&self->strings_mutex);
  if (!(ret = g_hash_table_lookup (self->strings, name)))
    {
      ret = g_ref_string_new (name);
      g_hash_table_insert (self->strings, ret, ret);
    }
  ret = g_ref_string_acquire (ret);
  g_mutex_unlock (&self->strings_mutex);

  return ret;
}

static void
sysprof_document_symbolize_symbols_cb (GObject      *object,
                                       GAsyncResult *result,
                                       gpointer      user_data)
{
  g_autoptr(SysprofDocumentSymbols) symbols = NULL;
  g_autoptr(GTask) task = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if ((symbols = _sysprof_document_symbols_new_finish (result, &error)))
    g_task_return_pointer (task, g_steal_pointer (&symbols), g_object_unref);
  else
    g_task_return_error (task, g_steal_pointer (&error));
}

static void
sysprof_document_symbolize_prepare_cb (GObject      *object,
                                       GAsyncResult *result,
                                       gpointer      user_data)
{
  SysprofSymbolizer *symbolizer = (SysprofSymbolizer *)object;
  g_autoptr(GTask) task = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (SYSPROF_IS_SYMBOLIZER (symbolizer));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (!_sysprof_symbolizer_prepare_finish (symbolizer, result, &error))
    g_task_return_error (task, g_steal_pointer (&error));
  else
    _sysprof_document_symbols_new (g_task_get_source_object (task),
                                   symbolizer,
                                   g_task_get_cancellable (task),
                                   sysprof_document_symbolize_symbols_cb,
                                   g_object_ref (task));
}

void
sysprof_document_symbolize_async (SysprofDocument     *self,
                                  SysprofSymbolizer   *symbolizer,
                                  GCancellable        *cancellable,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
  g_autoptr(SysprofDocumentSymbols) symbols = NULL;
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (SYSPROF_IS_DOCUMENT (self));
  g_return_if_fail (SYSPROF_IS_SYMBOLIZER (symbolizer));
  g_return_if_fail (!cancellable || SYSPROF_IS_SYMBOLIZER (symbolizer));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_document_symbolize_async);

  _sysprof_symbolizer_prepare_async (symbolizer,
                                     self,
                                     cancellable,
                                     sysprof_document_symbolize_prepare_cb,
                                     g_steal_pointer (&task));
}

SysprofDocumentSymbols *
sysprof_document_symbolize_finish (SysprofDocument  *self,
                                   GAsyncResult     *result,
                                   GError          **error)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);
  g_return_val_if_fail (G_IS_TASK (result), NULL);
  g_return_val_if_fail (g_task_is_valid (result, self), NULL);
  g_return_val_if_fail (g_task_get_source_tag (G_TASK (result)) == sysprof_document_symbolize_async, NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

gboolean
_sysprof_document_is_native (SysprofDocument *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), FALSE);

  return self->needs_swap == FALSE;
}

/**
 * sysprof_document_lookup_file:
 * @self: a #SysprofDocument
 * @path: the path of the file
 *
 * Locates @path within the document and returns a #SysprofDocumentFile if
 * it was found which can be used to access the contents.
 *
 * Returns: (transfer full) (nullable): a #SysprofDocumentFile
 */
SysprofDocumentFile *
sysprof_document_lookup_file (SysprofDocument *self,
                              const char      *path)
{
  gpointer key, value;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);
  g_return_val_if_fail (path != NULL, NULL);

  if (g_hash_table_lookup_extended (self->files_first_position, path, &key, &value))
    {
      g_autoptr(GPtrArray) file_chunks = g_ptr_array_new_with_free_func (g_object_unref);
      GtkBitsetIter iter;
      guint target = GPOINTER_TO_SIZE (value);
      guint i;

      if (gtk_bitset_iter_init_at (&iter, self->file_chunks, target, &i))
        {
          do
            {
              g_autoptr(SysprofDocumentFileChunk) file_chunk = sysprof_document_get_item ((GListModel *)self, i);

              if (g_strcmp0 (path, sysprof_document_file_chunk_get_path (file_chunk)) == 0)
                {
                  gboolean is_last = sysprof_document_file_chunk_get_is_last (file_chunk);

                  g_ptr_array_add (file_chunks, g_steal_pointer (&file_chunk));

                  if (is_last)
                    break;
                }
            }
          while (gtk_bitset_iter_next (&iter, &i));
        }

      return _sysprof_document_file_new (path, g_steal_pointer (&file_chunks));
    }

  return NULL;
}

/**
 * sysprof_document_list_files:
 * @self: a #SysprofDocument
 *
 * Gets a #GListModel of #SysprofDocumentFile
 *
 * Returns: (transfer full): a #GListModel
 */
GListModel *
sysprof_document_list_files (SysprofDocument *self)
{
  GHashTableIter hiter;
  GtkBitsetIter iter;
  GListStore *model;
  gpointer key, value;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  model = g_list_store_new (SYSPROF_TYPE_DOCUMENT_FILE);

  g_hash_table_iter_init (&hiter, self->files_first_position);
  while (g_hash_table_iter_next (&hiter, &key, &value))
    {
      g_autoptr(SysprofDocumentFile) file = NULL;
      g_autoptr(GPtrArray) file_chunks = g_ptr_array_new_with_free_func (g_object_unref);
      const char *path = key;
      guint target = GPOINTER_TO_SIZE (value);
      guint i;

      if (gtk_bitset_iter_init_at (&iter, self->file_chunks, target, &i))
        {
          do
            {
              g_autoptr(SysprofDocumentFileChunk) file_chunk = sysprof_document_get_item ((GListModel *)self, i);

              if (g_strcmp0 (path, sysprof_document_file_chunk_get_path (file_chunk)) == 0)
                {
                  gboolean is_last = sysprof_document_file_chunk_get_is_last (file_chunk);

                  g_ptr_array_add (file_chunks, g_steal_pointer (&file_chunk));

                  if (is_last)
                    break;
                }
            }
          while (gtk_bitset_iter_next (&iter, &i));
        }

      file = _sysprof_document_file_new (path, g_steal_pointer (&file_chunks));

      g_list_store_append (model, file);
    }

  return G_LIST_MODEL (model);
}

/**
 * sysprof_document_list_traceables:
 * @self: a #SysprofDocument
 *
 * Gets a #GListModel containing #SysprofDocumentTraceable found within
 * the #SysprofDocument.
 *
 * Returns: (transfer full): a #GListModel
 */
GListModel *
sysprof_document_list_traceables (SysprofDocument *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  return _sysprof_document_bitset_index_new (G_LIST_MODEL (self), self->traceables);
}

/**
 * sysprof_document_list_processes:
 * @self: a #SysprofDocument
 *
 * Gets a #GListModel containing #SysprofDocumentProcess found within
 * the #SysprofDocument.
 *
 * Returns: (transfer full): a #GListModel
 */
GListModel *
sysprof_document_list_processes (SysprofDocument *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  return _sysprof_document_bitset_index_new (G_LIST_MODEL (self), self->processes);
}
