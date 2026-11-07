/* sysprof-document-storage.c
 *
 * Copyright 2026 Christian Hergert
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

#include "sysprof-document-storage-private.h"

struct _SysprofDocumentReader
{
  GObject                 parent_instance;
  GThread                *owner;
  SysprofDocumentStorage *storage;
  GCancellable           *cancellable;
  EggBitset              *bits;
  EggBitsetIter           iter;
  guint                   last_position;
  guint                   last_frame;
  gboolean                have_position;
};

static void list_model_iface_init (GListModelInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofDocumentReader, _sysprof_document_reader, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

static void
_sysprof_document_reader_finalize (GObject *object)
{
  SysprofDocumentReader *self = SYSPROF_DOCUMENT_READER (object);

  g_clear_pointer (&self->bits, egg_bitset_unref);
  g_clear_pointer (&self->storage, _sysprof_document_storage_unref);
  g_clear_object (&self->cancellable);

  G_OBJECT_CLASS (_sysprof_document_reader_parent_class)->finalize (object);
}

static void
_sysprof_document_reader_class_init (SysprofDocumentReaderClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = _sysprof_document_reader_finalize;
}

static void
_sysprof_document_reader_init (SysprofDocumentReader *self)
{
}

static GType
reader_get_item_type (GListModel *model)
{
  return SYSPROF_TYPE_DOCUMENT_FRAME;
}

static guint
reader_get_n_items (GListModel *model)
{
  SysprofDocumentReader *self = SYSPROF_DOCUMENT_READER (model);

  g_assert (self->owner == g_thread_self ());

  if (_sysprof_document_reader_is_cancelled (self))
    return 0;

  return self->bits ? egg_bitset_get_size (self->bits)
                    : _sysprof_document_storage_get_n_frames (self->storage);
}

static gpointer
reader_get_item (GListModel *model,
                 guint       position)
{
  SysprofDocumentReader *self = SYSPROF_DOCUMENT_READER (model);
  guint frame = position;

  g_assert (self->owner == g_thread_self ());

  if (self->bits != NULL)
    {
      if (position >= egg_bitset_get_size (self->bits))
        return NULL;

      if (self->have_position && position == self->last_position + 1)
        {
          egg_bitset_iter_next (&self->iter, &frame);
        }
      else if (self->have_position && position == self->last_position)
        {
          frame = self->last_frame;
        }
      else
        {
          frame = egg_bitset_get_nth (self->bits, position);
          egg_bitset_iter_init_at (&self->iter, self->bits, frame, NULL);
        }

      self->last_position = position;
      self->last_frame = frame;
      self->have_position = TRUE;
    }

  return _sysprof_document_storage_get_frame (self->storage, frame);
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = reader_get_item_type;
  iface->get_n_items = reader_get_n_items;
  iface->get_item = reader_get_item;
}

SysprofDocumentReader *
_sysprof_document_reader_new (SysprofDocumentStorage *storage,
                              GCancellable           *cancellable)
{
  SysprofDocumentReader *self;

  g_return_val_if_fail (storage != NULL, NULL);

  self = g_object_new (SYSPROF_TYPE_DOCUMENT_READER, NULL);
  self->owner = g_thread_self ();
  self->storage = _sysprof_document_storage_ref (storage);

  if (cancellable != NULL)
    self->cancellable = g_object_ref (cancellable);

  return self;
}

GListModel *
_sysprof_document_reader_list_marks (SysprofDocumentReader *self,
                                     const char            *group)
{
  SysprofDocumentReader *reader;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_READER (self), NULL);

  reader = _sysprof_document_reader_new (self->storage, self->cancellable);
  reader->bits = _sysprof_document_storage_copy_marks (self->storage, group);

  return G_LIST_MODEL (reader);
}

guint
_sysprof_document_reader_get_count (SysprofDocumentReader *self,
                                    SysprofDocumentCount   count)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_READER (self), 0);

  return _sysprof_document_storage_get_count (self->storage, count);
}

const SysprofCounterMetadata *
_sysprof_document_reader_get_counters (SysprofDocumentReader *self,
                                       guint                 *n_counters)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_READER (self), NULL);

  return _sysprof_document_storage_get_counters (self->storage, n_counters);
}

gboolean
_sysprof_document_reader_is_cancelled (SysprofDocumentReader *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_READER (self), TRUE);

  return self->cancellable != NULL && g_cancellable_is_cancelled (self->cancellable);
}

GListModel *
_sysprof_document_reader_list_logs (SysprofDocumentReader *self)
{
  SysprofDocumentReader *reader;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_READER (self), NULL);

  reader = _sysprof_document_reader_new (self->storage, self->cancellable);
  reader->bits = _sysprof_document_storage_copy_logs (self->storage);

  return G_LIST_MODEL (reader);
}

GListModel *
_sysprof_document_reader_list_files (SysprofDocumentReader *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_READER (self), NULL);

  return _sysprof_document_storage_list_files (self->storage, self->cancellable);
}

const SysprofProcessMetadata *
_sysprof_document_reader_get_processes (SysprofDocumentReader *self,
                                        guint                 *n_processes)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_READER (self), NULL);

  return _sysprof_document_storage_get_processes (self->storage, n_processes);
}

const char *
_sysprof_document_reader_get_metadata (SysprofDocumentReader *self,
                                       const char            *key)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_READER (self), NULL);

  return _sysprof_document_storage_get_metadata (self->storage, key);
}

GListModel *
_sysprof_document_reader_list_named_marks (SysprofDocumentReader *self,
                                           const char *const     *names)
{
  SysprofDocumentReader *reader;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_READER (self), NULL);

  reader = _sysprof_document_reader_new (self->storage, self->cancellable);
  reader->bits = _sysprof_document_storage_copy_named_marks (self->storage, names);

  return G_LIST_MODEL (reader);
}

GCancellable *
_sysprof_document_reader_get_cancellable (SysprofDocumentReader *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_READER (self), NULL);

  return self->cancellable;
}
