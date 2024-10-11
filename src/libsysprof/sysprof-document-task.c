/*
 * sysprof-document-task.c
 *
 * Copyright 2024 Christian Hergert <chergert@redhat.com>
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

#include "sysprof-document-loader-private.h"
#include "sysprof-document-task-private.h"

typedef struct
{
  GMutex        mutex;
  char         *message;
  char         *title;
  double        progress;
  GCancellable *cancellable;
  guint         notify_source;
  GWeakRef      loader_wr;
} SysprofDocumentTaskPrivate;

enum {
  PROP_0,
  PROP_CANCELLED,
  PROP_MESSAGE,
  PROP_PROGRESS,
  PROP_TITLE,
  N_PROPS
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (SysprofDocumentTask, sysprof_document_task, G_TYPE_OBJECT)

static GParamSpec *properties[N_PROPS];

static void
sysprof_document_task_finalize (GObject *object)
{
  SysprofDocumentTask *self = (SysprofDocumentTask *)object;
  SysprofDocumentTaskPrivate *priv = sysprof_document_task_get_instance_private (self);

  g_mutex_clear (&priv->mutex);
  g_clear_handle_id (&priv->notify_source, g_source_remove);
  g_clear_pointer (&priv->message, g_free);
  g_clear_pointer (&priv->title, g_free);
  g_clear_object (&priv->cancellable);
  g_weak_ref_clear (&priv->loader_wr);

  G_OBJECT_CLASS (sysprof_document_task_parent_class)->finalize (object);
}

static void
sysprof_document_task_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  SysprofDocumentTask *self = SYSPROF_DOCUMENT_TASK (object);

  switch (prop_id)
    {
    case PROP_CANCELLED:
      g_value_set_boolean (value, sysprof_document_task_is_cancelled (self));
      break;

    case PROP_MESSAGE:
      g_value_take_string (value, sysprof_document_task_dup_message (self));
      break;

    case PROP_PROGRESS:
      g_value_set_double (value, sysprof_document_task_get_progress (self));
      break;

    case PROP_TITLE:
      g_value_take_string (value, sysprof_document_task_dup_title (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_document_task_class_init (SysprofDocumentTaskClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_document_task_finalize;
  object_class->get_property = sysprof_document_task_get_property;

  properties[PROP_CANCELLED] =
    g_param_spec_boolean ("cancelled", NULL, NULL,
                         FALSE,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_MESSAGE] =
    g_param_spec_string ("message", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_PROGRESS] =
    g_param_spec_double ("progress", NULL, NULL,
                         0., 1., 0.,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_TITLE] =
    g_param_spec_string ("title", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_document_task_init (SysprofDocumentTask *self)
{
  SysprofDocumentTaskPrivate *priv = sysprof_document_task_get_instance_private (self);

  g_weak_ref_init (&priv->loader_wr, NULL);

  priv->cancellable = g_cancellable_new ();
}

static gboolean
notify_in_idle_cb (gpointer data)
{
  SysprofDocumentTask *self = data;
  SysprofDocumentTaskPrivate *priv = sysprof_document_task_get_instance_private (self);

  g_assert (SYSPROF_IS_DOCUMENT_TASK (self));

  g_mutex_lock (&priv->mutex);
  priv->notify_source = 0;
  g_mutex_unlock (&priv->mutex);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CANCELLED]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MESSAGE]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROGRESS]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);

  return G_SOURCE_REMOVE;
}

static void
notify_in_idle_locked (SysprofDocumentTask *self)
{
  SysprofDocumentTaskPrivate *priv = sysprof_document_task_get_instance_private (self);

  g_assert (SYSPROF_IS_DOCUMENT_TASK (self));

  if (priv->notify_source == 0)
    priv->notify_source = g_idle_add_full (G_PRIORITY_LOW,
                                           notify_in_idle_cb,
                                           g_object_ref (self),
                                           g_object_unref);
}

char *
sysprof_document_task_dup_message (SysprofDocumentTask *self)
{
  SysprofDocumentTaskPrivate *priv = sysprof_document_task_get_instance_private (self);
  char *ret;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_TASK (self), NULL);

  g_mutex_lock (&priv->mutex);
  ret = g_strdup (priv->message);
  g_mutex_unlock (&priv->mutex);

  return ret;
}

char *
sysprof_document_task_dup_title (SysprofDocumentTask *self)
{
  SysprofDocumentTaskPrivate *priv = sysprof_document_task_get_instance_private (self);
  char *ret;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_TASK (self), NULL);

  g_mutex_lock (&priv->mutex);
  ret = g_strdup (priv->title);
  g_mutex_unlock (&priv->mutex);

  return ret;
}

void
_sysprof_document_task_take_message (SysprofDocumentTask *self,
                                     char                *message)
{
  SysprofDocumentTaskPrivate *priv = sysprof_document_task_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_DOCUMENT_TASK (self));

  g_mutex_lock (&priv->mutex);
  g_free (priv->message);
  priv->message = message;
  notify_in_idle_locked (self);
  g_mutex_unlock (&priv->mutex);
}

double
sysprof_document_task_get_progress (SysprofDocumentTask *self)
{
  SysprofDocumentTaskPrivate *priv = sysprof_document_task_get_instance_private (self);
  double ret;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_TASK (self), 0);

  g_mutex_lock (&priv->mutex);
  ret = priv->progress;
  g_mutex_unlock (&priv->mutex);

  return ret;
}

void
_sysprof_document_task_set_progress (SysprofDocumentTask *self,
                                     double               progress)
{
  SysprofDocumentTaskPrivate *priv = sysprof_document_task_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_DOCUMENT_TASK (self));

  progress = CLAMP (progress, 0., 1.);

  g_mutex_lock (&priv->mutex);
  priv->progress = progress;
  notify_in_idle_locked (self);
  g_mutex_unlock (&priv->mutex);
}

void
_sysprof_document_task_set_title (SysprofDocumentTask *self,
                                  const char          *title)
{
  SysprofDocumentTaskPrivate *priv = sysprof_document_task_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_DOCUMENT_TASK (self));

  g_mutex_lock (&priv->mutex);
  if (g_set_str (&priv->title, title))
    notify_in_idle_locked (self);
  g_mutex_unlock (&priv->mutex);
}

gboolean
sysprof_document_task_is_cancelled (SysprofDocumentTask *self)
{
  SysprofDocumentTaskPrivate *priv = sysprof_document_task_get_instance_private (self);
  gboolean ret;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_TASK (self), FALSE);

  g_mutex_lock (&priv->mutex);
  ret = g_cancellable_is_cancelled (priv->cancellable);
  g_mutex_unlock (&priv->mutex);

  return ret;
}

GCancellable *
_sysprof_document_task_get_cancellable (SysprofDocumentTask *self)
{
  SysprofDocumentTaskPrivate *priv = sysprof_document_task_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_TASK (self), NULL);

  return priv->cancellable;
}

SysprofDocumentTaskScope *
_sysprof_document_task_register (SysprofDocumentTask   *self,
                                 SysprofDocumentLoader *loader)
{
  SysprofDocumentTaskPrivate *priv = sysprof_document_task_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_TASK (self), NULL);
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_LOADER (loader), NULL);

  g_weak_ref_set (&priv->loader_wr, loader);

  _sysprof_document_loader_add_task (loader, self);

  return self;
}

void
_sysprof_document_task_unregister (SysprofDocumentTask *self)
{
  SysprofDocumentTaskPrivate *priv = sysprof_document_task_get_instance_private (self);
  g_autoptr(SysprofDocumentLoader) loader = NULL;

  g_return_if_fail (SYSPROF_IS_DOCUMENT_TASK (self));

  if ((loader = g_weak_ref_get (&priv->loader_wr)))
    _sysprof_document_loader_remove_task (loader, self);
}

void
sysprof_document_task_cancel (SysprofDocumentTask *self)
{
  SysprofDocumentTaskPrivate *priv = sysprof_document_task_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_DOCUMENT_TASK (self));

  g_cancellable_cancel (priv->cancellable);

  if (SYSPROF_DOCUMENT_TASK_GET_CLASS (self)->cancel)
    SYSPROF_DOCUMENT_TASK_GET_CLASS (self)->cancel (self);
}
