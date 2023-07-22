/* sysprof-session-model.c
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

#include "sysprof-session-model.h"
#include "sysprof-session-model-item.h"

struct _SysprofSessionModel
{
  GObject         parent_instance;
  GListModel     *model;
  GSignalGroup   *model_signals;
  SysprofSession *session;
};

enum {
  PROP_0,
  PROP_MODEL,
  PROP_SESSION,
  N_PROPS
};

static GType
sysprof_session_model_get_item_type (GListModel *model)
{
  return G_TYPE_OBJECT;
}

static gpointer
sysprof_session_model_get_item (GListModel *model,
                                guint       position)
{
  SysprofSessionModel *self = SYSPROF_SESSION_MODEL (model);
  g_autoptr(GObject) item = NULL;

  if (self->model == NULL)
    return NULL;

  if ((item = g_list_model_get_item (self->model, position)))
    return g_object_new (SYSPROF_TYPE_SESSION_MODEL_ITEM,
                         "session", self->session,
                         "item", item,
                         NULL);

  return NULL;
}

static guint
sysprof_session_model_get_n_items (GListModel *model)
{
  SysprofSessionModel *self = SYSPROF_SESSION_MODEL (model);

  if (self->model == NULL || self->session == 0)
    return 0;

  return g_list_model_get_n_items (self->model);
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = sysprof_session_model_get_item_type;
  iface->get_n_items = sysprof_session_model_get_n_items;
  iface->get_item = sysprof_session_model_get_item;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofSessionModel, sysprof_session_model, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

static GParamSpec *properties [N_PROPS];

static void
sysprof_session_model_items_changed_cb (SysprofSessionModel *self,
                                        guint                position,
                                        guint                removed,
                                        guint                added,
                                        GListModel          *model)
{
  g_assert (SYSPROF_IS_SESSION_MODEL (self));

  if (self->session == NULL || self->model != model)
    return;

  g_list_model_items_changed (G_LIST_MODEL (self), position, removed, added);
}

static void
sysprof_session_model_dispose (GObject *object)
{
  SysprofSessionModel *self = (SysprofSessionModel *)object;

  g_clear_object (&self->model_signals);
  g_clear_object (&self->session);
  g_clear_object (&self->model);

  G_OBJECT_CLASS (sysprof_session_model_parent_class)->dispose (object);
}

static void
sysprof_session_model_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  SysprofSessionModel *self = SYSPROF_SESSION_MODEL (object);

  switch (prop_id)
    {
    case PROP_SESSION:
      g_value_set_object (value, sysprof_session_model_get_session (self));
      break;

    case PROP_MODEL:
      g_value_set_object (value, sysprof_session_model_get_model (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_session_model_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  SysprofSessionModel *self = SYSPROF_SESSION_MODEL (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      sysprof_session_model_set_model (self, g_value_get_object (value));
      break;

    case PROP_SESSION:
      sysprof_session_model_set_session (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_session_model_class_init (SysprofSessionModelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = sysprof_session_model_dispose;
  object_class->get_property = sysprof_session_model_get_property;
  object_class->set_property = sysprof_session_model_set_property;

  properties [PROP_MODEL] =
    g_param_spec_object ("model", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_SESSION] =
    g_param_spec_object ("session", NULL, NULL,
                         SYSPROF_TYPE_SESSION,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_session_model_init (SysprofSessionModel *self)
{
  self->model_signals = g_signal_group_new (G_TYPE_LIST_MODEL);
  g_signal_group_connect_object (self->model_signals,
                                 "items-changed",
                                 G_CALLBACK (sysprof_session_model_items_changed_cb),
                                 self,
                                 G_CONNECT_SWAPPED);
}

SysprofSessionModel *
sysprof_session_model_new (SysprofSession *session,
                           GListModel     *model)
{
  g_return_val_if_fail (!session || SYSPROF_IS_SESSION (session), NULL);
  g_return_val_if_fail (!model || G_IS_LIST_MODEL (model), NULL);

  return g_object_new (SYSPROF_TYPE_SESSION_MODEL,
                       "session", session,
                       "model", model,
                       NULL);
}

/**
 * sysprof_model_model_get_model:
 * @self: a #SysprofSessionModel
 *
 * Returns: (transfer none) (nullable): a #GListModel or %NULL
 */
GListModel *
sysprof_session_model_get_model (SysprofSessionModel *self)
{
  g_return_val_if_fail (SYSPROF_IS_SESSION_MODEL (self), NULL);

  return self->model;
}

void
sysprof_session_model_set_model (SysprofSessionModel *self,
                                 GListModel          *model)
{
  guint old_n_items = 0;
  guint new_n_items = 0;

  g_return_if_fail (SYSPROF_IS_SESSION_MODEL (self));
  g_return_if_fail (!model || G_IS_LIST_MODEL (model));

  if (self->model == model)
    return;

  if (model)
    g_object_ref (model);

  old_n_items = g_list_model_get_n_items (G_LIST_MODEL (self));
  g_set_object (&self->model, model);
  g_signal_group_set_target (self->model_signals, model);
  new_n_items = g_list_model_get_n_items (G_LIST_MODEL (self));

  if (old_n_items || new_n_items)
    g_list_model_items_changed (G_LIST_MODEL (self), 0, old_n_items, new_n_items);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODEL]);
}

/**
 * sysprof_session_model_get_session:
 * @self: a #SysprofSessionModel
 *
 * Returns: (transfer none) (nullable): a #SysprofSession or %NULL
 */
SysprofSession *
sysprof_session_model_get_session (SysprofSessionModel *self)
{
  g_return_val_if_fail (SYSPROF_IS_SESSION_MODEL (self), NULL);

  return self->session;
}

void
sysprof_session_model_set_session (SysprofSessionModel *self,
                                   SysprofSession      *session)
{
  guint old_n_items = 0;
  guint new_n_items = 0;

  g_return_if_fail (SYSPROF_IS_SESSION_MODEL (self));
  g_return_if_fail (!session || SYSPROF_IS_SESSION (session));

  if (self->session == session)
    return;

  old_n_items = g_list_model_get_n_items (G_LIST_MODEL (self));
  g_set_object (&self->session, session);
  new_n_items = g_list_model_get_n_items (G_LIST_MODEL (self));

  if (old_n_items || new_n_items)
    g_list_model_items_changed (G_LIST_MODEL (self), 0, old_n_items, new_n_items);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SESSION]);
}
