/*
 * sysprof-document-process-list.c
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

#include "sysprof-document-process-list.h"

struct _SysprofDocumentProcessList
{
  GObject     parent_instance;
  GListModel *model;
};

static void list_model_iface_init (GListModelInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofDocumentProcessList, sysprof_document_process_list, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

enum {
  PROP_0,
  PROP_MODEL,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

/**
 * sysprof_document_process_list_new:
 * @model: a #GListModel or %NULL
 *
 * Creates a new #SysprofDocumentProcessList.
 *
 * The resulting process list can be used to determine the lifetime of
 * processes that were found within the document.
 *
 * Returns: (transfer full): a new #SysprofDocumentProcessList
 */
SysprofDocumentProcessList *
sysprof_document_process_list_new (GListModel *model)
{
  g_return_val_if_fail (!model || G_IS_LIST_MODEL (model), NULL);

  return g_object_new (SYSPROF_TYPE_DOCUMENT_PROCESS_LIST,
                       "model", model,
                       NULL);
}

static void
sysprof_document_process_list_finalize (GObject *object)
{
  SysprofDocumentProcessList *self = (SysprofDocumentProcessList *)object;

  g_clear_object (&self->model);

  G_OBJECT_CLASS (sysprof_document_process_list_parent_class)->finalize (object);
}

static void
sysprof_document_process_list_get_property (GObject    *object,
                                            guint       prop_id,
                                            GValue     *value,
                                            GParamSpec *pspec)
{
  SysprofDocumentProcessList *self = SYSPROF_DOCUMENT_PROCESS_LIST (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      g_value_set_object (value, sysprof_document_process_list_get_model (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_document_process_list_set_property (GObject      *object,
                                            guint         prop_id,
                                            const GValue *value,
                                            GParamSpec   *pspec)
{
  SysprofDocumentProcessList *self = SYSPROF_DOCUMENT_PROCESS_LIST (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      sysprof_document_process_list_set_model (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_document_process_list_class_init (SysprofDocumentProcessListClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_document_process_list_finalize;
  object_class->get_property = sysprof_document_process_list_get_property;
  object_class->set_property = sysprof_document_process_list_set_property;

  properties [PROP_MODEL] =
    g_param_spec_object ("model", NULL, NULL,
                         G_TYPE_OBJECT,
                         (G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_document_process_list_init (SysprofDocumentProcessList *self)
{
}

static void
list_model_iface_init (GListModelInterface *iface)
{
}

/**
 * sysprof_document_process_list_get_model:
 * @self: a #SysprofDocumentProcessList
 *
 * Gets the underlying model containing processes to extract.
 *
 * Returns: (transfer none) (nullable): a #GListModel or %NULL
 */
GListModel *
sysprof_document_process_list_get_model (SysprofDocumentProcessList *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_PROCESS_LIST (self), NULL);

  return self->model;
}

void
sysprof_document_process_list_set_model (SysprofDocumentProcessList *self,
                                         GListModel                 *model)
{
  g_return_if_fail (SYSPROF_IS_DOCUMENT_PROCESS_LIST (self));
  g_return_if_fail (!model || G_IS_LIST_MODEL (model));

  if (g_set_object (&self->model, model))
    {
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_MODEL]);
    }
}
