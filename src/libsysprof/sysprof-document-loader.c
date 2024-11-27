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

#include <errno.h>
#include <unistd.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include "sysprof-bundled-symbolizer.h"
#include "sysprof-debuginfod-symbolizer.h"
#include "sysprof-document-bitset-index-private.h"
#include "sysprof-document-loader-private.h"
#include "sysprof-document-private.h"
#include "sysprof-elf-symbolizer.h"
#include "sysprof-jitmap-symbolizer.h"
#include "sysprof-kallsyms-symbolizer.h"
#include "sysprof-multi-symbolizer.h"
#include "sysprof-symbolizer-private.h"

struct _SysprofDocumentLoader
{
  GObject            parent_instance;
  GMutex             mutex;
  GListStore        *tasks;
  SysprofSymbolizer *symbolizer;
  char              *filename;
  char              *message;
  double             fraction;
  int                fd;
  guint              notify_source;
  guint              symbolizing : 1;
};

enum {
  PROP_0,
  PROP_FRACTION,
  PROP_MESSAGE,
  PROP_SYMBOLIZER,
  PROP_TASKS,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofDocumentLoader, sysprof_document_loader, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static gboolean
progress_notify_in_idle (gpointer data)
{
  SysprofDocumentLoader *self = data;

  g_assert (SYSPROF_IS_DOCUMENT_LOADER (self));

  g_mutex_lock (&self->mutex);
  self->notify_source = 0;
  g_mutex_unlock (&self->mutex);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FRACTION]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MESSAGE]);

  return G_SOURCE_REMOVE;
}

static void
set_progress (double      fraction,
              const char *message,
              gpointer    user_data)
{
  SysprofDocumentLoader *self = user_data;

  g_assert (SYSPROF_IS_DOCUMENT_LOADER (self));

  g_mutex_lock (&self->mutex);

  self->fraction = fraction * .5;

  if (self->symbolizing)
    self->fraction += .5;

  g_set_str (&self->message, message);

  if (!self->notify_source)
    self->notify_source = g_idle_add_full (G_PRIORITY_LOW,
                                           progress_notify_in_idle,
                                           g_object_ref (self),
                                           g_object_unref);
  g_mutex_unlock (&self->mutex);
}

static void
mapped_file_by_filename (GTask        *task,
                         gpointer      source_object,
                         gpointer      task_data,
                         GCancellable *cancellable)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GMappedFile) mapped_file = g_mapped_file_new (task_data, FALSE, &error);

  if (mapped_file != NULL)
    g_task_return_pointer (task,
                           g_steal_pointer (&mapped_file),
                           (GDestroyNotify)g_mapped_file_unref);
  else
    g_task_return_error (task, g_steal_pointer (&error));
}

static void
mapped_file_new_async (const char          *filename,
                       GCancellable        *cancellable,
                       GAsyncReadyCallback  callback,
                       gpointer             user_data)
{
  g_autoptr(GTask) task = g_task_new (NULL, cancellable, callback, user_data);
  g_task_set_task_data (task, g_strdup (filename), g_free);
  g_task_run_in_thread (task, mapped_file_by_filename);
}

static void
mapped_file_by_fd (GTask        *task,
                   gpointer      source_object,
                   gpointer      task_data,
                   GCancellable *cancellable)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GMappedFile) mapped_file = g_mapped_file_new_from_fd (GPOINTER_TO_INT (task_data), FALSE, &error);

  if (mapped_file != NULL)
    g_task_return_pointer (task,
                           g_steal_pointer (&mapped_file),
                           (GDestroyNotify)g_mapped_file_unref);
  else
    g_task_return_error (task, g_steal_pointer (&error));
}

static void
close_fd (gpointer data)
{
  int fd = GPOINTER_TO_INT (data);
  close (fd);
}

static void
mapped_file_new_from_fd_async (int                  fd,
                               GCancellable        *cancellable,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  int copy_fd;

  task = g_task_new (NULL, cancellable, callback, user_data);

  if (-1 == (copy_fd = dup (fd)))
    {
      int errsv = errno;
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               g_io_error_from_errno (errsv),
                               "%s",
                               g_strerror (errsv));
      return;
    }

  g_task_set_task_data (task, GINT_TO_POINTER (copy_fd), close_fd);
  g_task_run_in_thread (task, mapped_file_by_fd);
}

static GMappedFile *
mapped_file_new_finish (GAsyncResult  *result,
                        GError       **error)
{
  return g_task_propagate_pointer (G_TASK (result), error);
}

static void
set_default_symbolizer (SysprofDocumentLoader *self)
{
  g_autoptr(SysprofMultiSymbolizer) multi = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (SYSPROF_IS_DOCUMENT_LOADER (self));

  g_clear_object (&self->symbolizer);

  multi = sysprof_multi_symbolizer_new ();
  sysprof_multi_symbolizer_take (multi, sysprof_bundled_symbolizer_new ());
  sysprof_multi_symbolizer_take (multi, sysprof_kallsyms_symbolizer_new ());
  sysprof_multi_symbolizer_take (multi, sysprof_elf_symbolizer_new ());
  sysprof_multi_symbolizer_take (multi, sysprof_jitmap_symbolizer_new ());

#if HAVE_DEBUGINFOD
  {
    g_autoptr(SysprofSymbolizer) debuginfod = NULL;

    if (!(debuginfod = sysprof_debuginfod_symbolizer_new (&error)))
      g_warning ("Failed to create debuginfod symbolizer: %s", error->message);
    else
      sysprof_multi_symbolizer_take (multi, g_steal_pointer (&debuginfod));
  }
#endif

  self->symbolizer = SYSPROF_SYMBOLIZER (g_steal_pointer (&multi));
}

static void
sysprof_document_loader_dispose (GObject *object)
{
  SysprofDocumentLoader *self = (SysprofDocumentLoader *)object;

  g_list_store_remove_all (self->tasks);

  G_OBJECT_CLASS (sysprof_document_loader_parent_class)->dispose (object);
}

static void
sysprof_document_loader_finalize (GObject *object)
{
  SysprofDocumentLoader *self = (SysprofDocumentLoader *)object;

  g_clear_handle_id (&self->notify_source, g_source_remove);
  g_clear_object (&self->symbolizer);
  g_clear_object (&self->tasks);
  g_clear_pointer (&self->filename, g_free);
  g_clear_pointer (&self->message, g_free);
  g_clear_fd (&self->fd, NULL);
  g_mutex_clear (&self->mutex);

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
      g_value_take_string (value, sysprof_document_loader_dup_message (self));
      break;

    case PROP_SYMBOLIZER:
      g_value_set_object (value, sysprof_document_loader_get_symbolizer (self));
      break;

    case PROP_TASKS:
      g_value_take_object (value, sysprof_document_loader_list_tasks (self));
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

  object_class->dispose = sysprof_document_loader_dispose;
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

  properties[PROP_TASKS] =
    g_param_spec_object ("tasks", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  g_type_ensure (SYSPROF_TYPE_DOCUMENT);
  g_type_ensure (SYSPROF_TYPE_DOCUMENT_BITSET_INDEX);
}

static void
sysprof_document_loader_init (SysprofDocumentLoader *self)
{
  g_mutex_init (&self->mutex);

  self->fd = -1;
  self->tasks = g_list_store_new (SYSPROF_TYPE_DOCUMENT_TASK);

  set_default_symbolizer (self);
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

  self = g_object_new (SYSPROF_TYPE_DOCUMENT_LOADER, NULL);

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
    {
      if (self->symbolizer == NULL)
        set_default_symbolizer (self);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_SYMBOLIZER]);
    }
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
 * Returns: (nullable): %NULL
 */
const char *
sysprof_document_loader_get_message (SysprofDocumentLoader *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_LOADER (self), NULL);

  return NULL;
}

/**
 * sysprof_document_loader_dup_message:
 * @self: a #SysprofDocumentLoader
 *
 * Gets a text message representing what is happenin with loading.
 *
 * This only updates between calls of sysprof_document_loader_load_async()
 * and sysprof_document_loader_load_finish().
 *
 * Returns: (nullable) (transfer full): a string or %NULL
 */
char *
sysprof_document_loader_dup_message (SysprofDocumentLoader *self)
{
  char *ret;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_LOADER (self), NULL);

  g_mutex_lock (&self->mutex);
  ret = g_strdup (self->message);
  g_mutex_unlock (&self->mutex);

  return ret;
}

static void
sysprof_document_loader_load_symbols_cb (GObject      *object,
                                         GAsyncResult *result,
                                         gpointer      user_data)
{
  SysprofDocument *document = (SysprofDocument *)object;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;
  SysprofDocumentLoader *self;

  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);

  set_progress (0., _("Document loaded"), self);

  if (!_sysprof_document_symbolize_finish (document, result, &error))
    g_task_return_error (task, g_steal_pointer (&error));
  else
    g_task_return_pointer (task, g_object_ref (document), g_object_unref);
}

static void
sysprof_document_loader_load_document_cb (GObject      *object,
                                          GAsyncResult *result,
                                          gpointer      user_data)
{
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;
  SysprofDocumentLoader *self;
  SysprofSymbolizer *symbolizer;

  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  symbolizer = g_task_get_task_data (task);

  g_assert (symbolizer != NULL);
  g_assert (SYSPROF_IS_SYMBOLIZER (symbolizer));

  if (!(document = _sysprof_document_new_finish (result, &error)))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      set_progress (1., _("Loading failed"), self);
      return;
    }

  if (self->filename != NULL)
    {
      g_autofree char *title = g_path_get_basename (self->filename);
      _sysprof_document_set_title (document, title);
    }

  self->symbolizing = TRUE;

  set_progress (.0, _("Symbolizing stack traces"), self);

  _sysprof_document_symbolize_async (document,
                                     symbolizer,
                                     set_progress,
                                     g_object_ref (self),
                                     g_object_unref,
                                     g_task_get_cancellable (task),
                                     sysprof_document_loader_load_symbols_cb,
                                     g_object_ref (task));
}

static void
sysprof_document_loader_load_mapped_file_cb (GObject      *object,
                                             GAsyncResult *result,
                                             gpointer      user_data)
{
  g_autoptr(GMappedFile) mapped_file = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;
  SysprofDocumentLoader *self;

  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);

  if (!(mapped_file = mapped_file_new_finish (result, &error)))
    g_task_return_error (task, g_steal_pointer (&error));
  else
    _sysprof_document_new_async (mapped_file,
                                 set_progress,
                                 g_object_ref (self),
                                 g_object_unref,
                                 g_task_get_cancellable (task),
                                 sysprof_document_loader_load_document_cb,
                                 g_object_ref (task));
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
  g_autoptr(SysprofSymbolizer) symbolizer = NULL;
  g_autoptr(GMappedFile) mapped_file = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (SYSPROF_IS_DOCUMENT_LOADER (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));
  g_return_if_fail (self->filename != NULL || self->fd != -1);

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_task_data (task, g_object_ref (self->symbolizer), g_object_unref);
  g_task_set_source_tag (task, sysprof_document_loader_load_async);

  set_progress (0., _("Loading document"), self);

  _sysprof_symbolizer_setup (self->symbolizer, self);

  if (self->fd != -1)
    mapped_file_new_from_fd_async (self->fd,
                                   cancellable,
                                   sysprof_document_loader_load_mapped_file_cb,
                                   g_steal_pointer (&task));
  else
    mapped_file_new_async (self->filename,
                           cancellable,
                           sysprof_document_loader_load_mapped_file_cb,
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

  set_progress (1., NULL, self);

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

typedef struct _TaskOp
{
  SysprofDocumentLoader *loader;
  SysprofDocumentTask *task;
  guint remove : 1;
} TaskOp;

static void
_g_list_store_remove (GListStore *store,
                      gpointer    instance)
{
  GListModel *model = G_LIST_MODEL (store);
  guint n_items = g_list_model_get_n_items (model);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(GObject) element = g_list_model_get_item (model, i);

      if (element == instance)
        {
          g_list_store_remove (store, i);
          return;
        }
    }
}

static gboolean
task_op_run (gpointer data)
{
  TaskOp *op = data;

  if (op->remove)
    _g_list_store_remove (op->loader->tasks, op->task);
  else
    g_list_store_append (op->loader->tasks, op->task);

  g_clear_object (&op->loader);
  g_clear_object (&op->task);
  g_free (op);

  return G_SOURCE_REMOVE;
}

static TaskOp *
task_op_new (SysprofDocumentLoader *loader,
             SysprofDocumentTask   *task,
             gboolean               remove)
{
  TaskOp op = {
    g_object_ref (loader),
    g_object_ref (task),
    !!remove
  };

  return g_memdup2 (&op, sizeof op);
}

void
_sysprof_document_loader_add_task (SysprofDocumentLoader *self,
                                   SysprofDocumentTask   *task)
{
  g_return_if_fail (SYSPROF_IS_DOCUMENT_LOADER (self));
  g_return_if_fail (SYSPROF_IS_DOCUMENT_TASK (task));

  g_idle_add (task_op_run, task_op_new (self, task, FALSE));
}

void
_sysprof_document_loader_remove_task (SysprofDocumentLoader *self,
                                      SysprofDocumentTask   *task)
{
  g_return_if_fail (SYSPROF_IS_DOCUMENT_LOADER (self));
  g_return_if_fail (SYSPROF_IS_DOCUMENT_TASK (task));

  g_idle_add (task_op_run, task_op_new (self, task, TRUE));
}

/**
 * sysprof_document_loader_list_tasks:
 * @self: a #SysprofDocumentLoader
 *
 * Returns: (transfer full): a #GListModel of #SysprofDocumentTask.
 */
GListModel *
sysprof_document_loader_list_tasks (SysprofDocumentLoader *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_LOADER (self), NULL);

  return g_object_ref (G_LIST_MODEL (self->tasks));
}

static void
sysprof_document_loader_load_cb (GObject      *object,
                                 GAsyncResult *result,
                                 gpointer      user_data)
{
  SysprofDocumentLoader *loader = (SysprofDocumentLoader *)object;
  g_autoptr(DexPromise) promise = user_data;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (SYSPROF_IS_DOCUMENT_LOADER (loader));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (DEX_IS_PROMISE (promise));

  if ((document = sysprof_document_loader_load_finish (loader, result, &error)))
    dex_promise_resolve_object (promise, g_steal_pointer (&document));
  else
    dex_promise_reject (promise, g_steal_pointer (&error));
}

DexFuture *
_sysprof_document_loader_load (SysprofDocumentLoader *loader)
{
  DexPromise *promise;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_LOADER (loader), NULL);

  promise = dex_promise_new ();
  sysprof_document_loader_load_async (loader,
                                      NULL,
                                      sysprof_document_loader_load_cb,
                                      dex_ref (promise));
  return DEX_FUTURE (promise);
}
