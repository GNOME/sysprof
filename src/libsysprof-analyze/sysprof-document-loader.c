/* sysprof-document-loader.c
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

#include "sysprof-document-loader.h"
#include "sysprof-document-private.h"

struct _SysprofDocumentLoader
{
  GObject            parent_instance;
  SysprofSymbolizer *symbolizer;
  char              *filename;
  char              *message;
  double             fraction;
  int                fd;
};

enum {
  PROP_0,
  PROP_FRACTION,
  PROP_MESSAGE,
  PROP_SYMBOLIZER,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofDocumentLoader, sysprof_document_loader, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static void
sysprof_document_loader_finalize (GObject *object)
{
  SysprofDocumentLoader *self = (SysprofDocumentLoader *)object;

  g_clear_object (&self->symbolizer);
  g_clear_pointer (&self->filename, g_free);
  g_clear_pointer (&self->message, g_free);

  if (self->fd != -1)
    {
      close (self->fd);
      self->fd = -1;
    }

  G_OBJECT_CLASS (sysprof_document_loader_parent_class)->finalize (object);
}

static void
sysprof_document_loader_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  SysprofDocumentLoader *self = SYSPROF_DOCUMENT_LOADER (object);

  switch (prop_id)
    {
    case PROP_FRACTION:
      g_value_set_double (value, sysprof_document_loader_get_fraction (self));
      break;

    case PROP_MESSAGE:
      g_value_set_string (value, sysprof_document_loader_get_message (self));
      break;

    case PROP_SYMBOLIZER:
      g_value_set_object (value, sysprof_document_loader_get_symbolizer (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_document_loader_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  SysprofDocumentLoader *self = SYSPROF_DOCUMENT_LOADER (object);

  switch (prop_id)
    {
    case PROP_SYMBOLIZER:
      sysprof_document_loader_set_symbolizer (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_document_loader_class_init (SysprofDocumentLoaderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_document_loader_finalize;
  object_class->get_property = sysprof_document_loader_get_property;
  object_class->set_property = sysprof_document_loader_set_property;

  properties [PROP_FRACTION] =
    g_param_spec_double ("fraction", NULL, NULL,
                         0, 1, 0,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_MESSAGE] =
    g_param_spec_string ("message", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SYMBOLIZER] =
    g_param_spec_object ("symbolizer", NULL, NULL,
                         SYSPROF_TYPE_SYMBOLIZER,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_document_loader_init (SysprofDocumentLoader *self)
{
  self->fd = -1;
}

SysprofDocumentLoader *
sysprof_document_loader_new (const char *filename)
{
  SysprofDocumentLoader *self;

  g_return_val_if_fail (filename != NULL, NULL);

  self = g_object_new (SYSPROF_TYPE_DOCUMENT_LOADER, NULL);
  self->filename = g_strdup (filename);

  return self;
}

SysprofDocumentLoader *
sysprof_document_loader_new_for_fd (int      fd,
                                    GError **error)
{
  g_autoptr(SysprofDocumentLoader) self = NULL;

  self = g_object_new (SYSPROF_TYPE_SYMBOLIZER, NULL);

  if (-1 == (self->fd = dup (fd)))
    {
      int errsv = errno;
      g_set_error_literal (error,
                           G_IO_ERROR,
                           g_io_error_from_errno (errsv),
                           g_strerror (errsv));
      return NULL;
    }

  return g_steal_pointer (&self);
}

/**
 * sysprof_document_loader_get_symbolizer:
 * @self: a #SysprofDocumentLoader
 *
 * Gets the #SysprofSymbolizer to use to symbolize traces within
 * the document.
 *
 * Returns: (transfer none) (nullable): a #SysprofSymbolizer or %NULL
 */
SysprofSymbolizer *
sysprof_document_loader_get_symbolizer (SysprofDocumentLoader *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_LOADER (self), NULL);

  return self->symbolizer;
}

/**
 * sysprof_document_loader_set_symbolizer:
 * @self: a #SysprofDocumentLoader
 * @symbolizer: (nullable): a #SysprofSymbolizer or %NULL
 *
 * Sets the symbolizer to use to convert instruction pointers to
 * symbol names in the document.
 *
 * If set to %NULL, a sensible default will be chosen when loading.
 */
void
sysprof_document_loader_set_symbolizer (SysprofDocumentLoader *self,
                                        SysprofSymbolizer     *symbolizer)
{
  g_return_if_fail (SYSPROF_IS_DOCUMENT_LOADER (self));

  if (g_set_object (&self->symbolizer, symbolizer))
    g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_SYMBOLIZER]);
}

/**
 * sysprof_document_loader_get_fraction:
 * @self: a #SysprofDocumentLoader
 *
 * Gets the fraction between 0 and 1 for the loading that has occurred.
 *
 * Returns: A value between 0 and 1.
 */
double
sysprof_document_loader_get_fraction (SysprofDocumentLoader *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_LOADER (self), .0);

  return self->fraction;
}

/**
 * sysprof_document_loader_get_message:
 * @self: a #SysprofDocumentLoader
 *
 * Gets a text message representing what is happenin with loading.
 *
 * This only updates between calls of sysprof_document_loader_load_async()
 * and sysprof_document_loader_load_finish().
 *
 * Returns: (nullable): a string containing a load message
 */
const char *
sysprof_document_loader_get_message (SysprofDocumentLoader *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_LOADER (self), NULL);

  return self->message;
}

static void
sysprof_document_loader_load_cb (GObject      *object,
                                 GAsyncResult *result,
                                 gpointer      user_data)
{
  SysprofDocument *document = (SysprofDocument *)object;
  g_autoptr(SysprofDocumentSymbols) symbols = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;

  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (!(symbols = _sysprof_document_symbolize_finish (document, result, &error)))
    g_task_return_error (task, g_steal_pointer (&error));
  else
    g_task_return_pointer (task, g_object_ref (document), g_object_unref);
}

/**
 * sysprof_document_loader_load_async:
 * @self: a #SysprofDocumentLoader
 * @cancellable: (nullable): a #GCancellable or %NULL
 * @callback: a callback to execute upon completion
 * @user_data: closure data for @callback
 *
 * Asynchronously loads the document.
 *
 * @callback should call sysprof_document_loader_load_finish().
 */
void
sysprof_document_loader_load_async (SysprofDocumentLoader *self,
                                    GCancellable          *cancellable,
                                    GAsyncReadyCallback    callback,
                                    gpointer               user_data)
{
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (SYSPROF_IS_DOCUMENT_LOADER (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_document_loader_load_async);

  if (self->fd == -1 && self->filename == NULL)
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_FAILED,
                               "SysprofDocumentLoader disposition must have fd or filename set");
      return;
    }

  if (self->fd != -1)
    document = _sysprof_document_new_from_fd (self->fd, &error);
  else if (self->filename != NULL)
    document = _sysprof_document_new (self->filename, &error);
  else
    g_assert_not_reached ();

  /* TODO: This will probably get renamed to load_async as we want
   * to always have a symbolizer for loading. Additionally, the
   * document will deal with caches directly instead of the symbols
   * object.
   */

  _sysprof_document_symbolize_async (document,
                                     self->symbolizer,
                                     cancellable,
                                     sysprof_document_loader_load_cb,
                                     g_steal_pointer (&task));
}

/**
 * sysprof_document_loader_load_finish:
 * @self: a #SysprofDocumentLoader
 * @result: a #GAsyncResult
 * @error: a location for a #GError, or %NULL
 *
 * Completes a request to load a document asynchronously.
 *
 * Returns: (transfer full): a #SysprofDocumentLoader or %NULL
 *   and @error is set.
 */
SysprofDocument *
sysprof_document_loader_load_finish (SysprofDocumentLoader  *self,
                                     GAsyncResult           *result,
                                     GError                **error)
{
  SysprofDocument *ret;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_LOADER (self), NULL);
  g_return_val_if_fail (G_IS_TASK (result), NULL);
  g_return_val_if_fail (g_task_is_valid (result, self), NULL);

  ret = g_task_propagate_pointer (G_TASK (result), error);

  g_return_val_if_fail (!ret || SYSPROF_IS_DOCUMENT (ret), NULL);

  return ret;
}

typedef struct SysprofDocumentLoaderSync
{
  GMainContext *main_context;
  SysprofDocument *document;
  GError *error;
} SysprofDocumentLoaderSync;

static void
sysprof_document_loader_load_sync_cb (GObject      *object,
                                      GAsyncResult *result,
                                      gpointer      user_data)
{
  SysprofDocumentLoader *loader = (SysprofDocumentLoader *)object;
  SysprofDocumentLoaderSync *state = user_data;

  g_assert (SYSPROF_IS_DOCUMENT_LOADER (loader));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (state != NULL);
  g_assert (state->main_context != NULL);

  state->document = sysprof_document_loader_load_finish (loader, result, &state->error);

  g_main_context_wakeup (state->main_context);
}

/**
 * sysprof_document_loader_load:
 * @self: a #SysprofDocumentLoader
 * @cancellable: (nullable): a #GCancellable or %NULL
 * @error: a location for a #GError, or %NULL
 *
 * Synchronously loads the document.
 *
 * This function requires a #GMainContext to be set for the current
 * thread and uses the asynchronously loader API underneath.
 *
 * Returns: (transfer full): a #SysprofDocument if successful; otherwise
 *   %NULL and @error is set.
 */
SysprofDocument *
sysprof_document_loader_load (SysprofDocumentLoader  *self,
                              GCancellable           *cancellable,
                              GError                **error)
{
  SysprofDocumentLoaderSync state;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_LOADER (self), NULL);
  g_return_val_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable), NULL);

  state.main_context = g_main_context_ref_thread_default ();
  state.document = NULL;
  state.error = NULL;

  sysprof_document_loader_load_async (self,
                                      cancellable,
                                      sysprof_document_loader_load_sync_cb,
                                      &state);

  while (state.document == NULL && state.error == NULL)
    g_main_context_iteration (state.main_context, TRUE);

  g_main_context_unref (state.main_context);

  if (state.error != NULL)
    g_propagate_error (error, state.error);

  return state.document;
}
