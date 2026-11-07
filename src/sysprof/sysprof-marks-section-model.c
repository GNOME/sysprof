/*
 * sysprof-marks-section-model.c
 *
 * Copyright 2025 Georges Basile Stavracas Neto <feaneron@igalia.com>
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

#include <glib/gi18n.h>
#include <sysprof-collector.h>

#include "sysprof-marks-section-model.h"
#include "sysprof-mark-preparation-private.h"
#include "sysprof-document-bitset-index-private.h"

#include "sysprof-expression-map-model.h"
#include "sysprof-marks-section-model-item.h"

struct _SysprofMarksSectionModel
{
  GObject         parent_instance;

  SysprofSession *session;
  GPtrArray      *prepared;
  GListModel     *summary;
  DexFuture      *preparation;
  DexFuture      *selection;
  guint           generation;
  guint           selection_generation;
  guint           update_source;
  gulong          changed_handler;
  gboolean        loading;
  char           *error;

  GListModel     *groups;
  GListModel     *filtered_groups;

  GListModel     *catalog;
  GListModel     *filtered_catalog;

  GListModel     *marks;
  GListModel     *filtered_marks;
};

static GType
sysprof_marks_section_model_get_item_type (GListModel *model)
{
  return G_TYPE_OBJECT;
}

static gpointer
sysprof_marks_section_model_get_item (GListModel *model,
                                      guint       position)
{
  SysprofMarksSectionModel *self = (SysprofMarksSectionModel *) model;

  if (self->filtered_marks == NULL)
    return NULL;

  return g_list_model_get_item (self->filtered_marks, position);
}

static guint
sysprof_marks_section_model_get_n_items (GListModel *model)
{
  SysprofMarksSectionModel *self = (SysprofMarksSectionModel *) model;

  if (self->filtered_marks == NULL)
    return 0;

  return g_list_model_get_n_items (self->filtered_marks);
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = sysprof_marks_section_model_get_item_type;
  iface->get_n_items = sysprof_marks_section_model_get_n_items;
  iface->get_item = sysprof_marks_section_model_get_item;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofMarksSectionModel, sysprof_marks_section_model, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

enum
{
  PROP_0,
  PROP_FILTERED_GROUPS,
  PROP_FILTERED_MARK_CATALOG,
  PROP_FILTERED_MARKS,
  PROP_GROUPS,
  PROP_MARK_CATALOG,
  PROP_MARKS,
  PROP_SESSION,
  PROP_SUMMARY,
  PROP_LOADING,
  PROP_STATUS,
  PROP_ERROR,
  N_PROPS,
};

static GParamSpec *properties[N_PROPS];

static void
set_state (SysprofMarksSectionModel *self,
           gboolean                  loading,
           const char               *error)
{
  self->loading = loading;
  g_free (self->error);
  self->error = g_strdup (error);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOADING]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATUS]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ERROR]);
}

static void
sysprof_marks_section_model_disconnect (SysprofMarksSectionModel *self)
{
  guint removed = g_list_model_get_n_items (G_LIST_MODEL (self));

  self->generation++;
  self->selection_generation++;
  g_clear_handle_id (&self->update_source, g_source_remove);
  dex_clear (&self->preparation);
  dex_clear (&self->selection);
  g_clear_signal_handler (&self->changed_handler, self->filtered_catalog);
  g_clear_pointer (&self->prepared, g_ptr_array_unref);
  g_clear_object (&self->summary);
  g_clear_object (&self->catalog);
  g_clear_object (&self->filtered_catalog);
  g_clear_object (&self->filtered_groups);
  g_clear_object (&self->filtered_marks);
  g_clear_object (&self->groups);
  g_clear_object (&self->marks);

  if (removed)
    g_list_model_items_changed (G_LIST_MODEL (self), 0, removed, 0);

  for (guint i = PROP_FILTERED_GROUPS; i <= PROP_MARKS; i++)
    g_object_notify_by_pspec (G_OBJECT (self), properties[i]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SUMMARY]);
  set_state (self, FALSE, NULL);
}

typedef struct
{
  GWeakRef model;
  guint    generation;
  guint    selection_generation;
} Completion;

static Completion *
completion_new (SysprofMarksSectionModel *self)
{
  Completion *completion = g_new0 (Completion, 1);

  g_weak_ref_init (&completion->model, self);
  completion->generation = self->generation;
  completion->selection_generation = self->selection_generation;

  return completion;
}

static void
completion_free (gpointer data)
{
  Completion *completion = data;

  g_weak_ref_clear (&completion->model);
  g_free (completion);
}

static DexFuture *
selection_ready (DexFuture *future,
                 gpointer   data)
{
  Completion *completion = data;
  g_autoptr(SysprofMarksSectionModel) self = g_weak_ref_get (&completion->model);
  g_autoptr(GError) error = NULL;
  const GValue *value;

  if (self == NULL ||
      self->generation != completion->generation ||
      self->selection_generation != completion->selection_generation)
    return NULL;

  if ((value = dex_future_get_value (future, &error)))
    {
      GPtrArray *result = g_value_get_boxed (value);
      guint removed = g_list_model_get_n_items (G_LIST_MODEL (self));
      gint64 begin = SYSPROF_CAPTURE_CURRENT_TIME;

      g_clear_object (&self->filtered_marks);
      self->filtered_marks = _sysprof_document_bitset_index_new_full (
          G_LIST_MODEL (sysprof_session_get_document (self->session)),
          g_ptr_array_index (result, 0), SYSPROF_TYPE_DOCUMENT_MARK);
      g_list_model_items_changed (G_LIST_MODEL (self),
                                  0,
                                  removed,
                                  g_list_model_get_n_items (self->filtered_marks));
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILTERED_MARKS]);
      set_state (self, FALSE, NULL);
      sysprof_collector_mark (begin,
                              SYSPROF_CAPTURE_CURRENT_TIME - begin,
                              "Sysprof",
                              "Publish ordered marks",
                              NULL);
    }
  else if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
           !g_error_matches (error, DEX_ERROR, DEX_ERROR_FIBER_CANCELLED))
    {
      set_state (self, FALSE, error->message);
    }

  return NULL;
}

static gboolean
update_selection (gpointer data)
{
  SysprofMarksSectionModel *self = data;
  g_autoptr(GArray) selected = g_array_new (FALSE, FALSE, sizeof (guint));

  self->update_source = 0;

  for (guint i = 0; i < self->prepared->len; i++)
    {
      g_autoptr(SysprofMarksSectionModelItem) item = g_list_model_get_item (self->catalog, i);

      if (sysprof_marks_section_model_item_get_visible (item))
        g_array_append_val (selected, i);
    }

  self->selection = dex_future_finally (_sysprof_mark_prepare_union (self->prepared, selected),
                                        selection_ready, completion_new (self), completion_free);

  return G_SOURCE_REMOVE;
}

static void
queue_selection (SysprofMarksSectionModel *self)
{
  self->selection_generation++;
  dex_clear (&self->selection);
  set_state (self, TRUE, NULL);

  if (self->update_source == 0)
    self->update_source = g_idle_add (update_selection, self);
}

static gpointer
map_func (gpointer item)
{
  return sysprof_marks_section_model_item_new (item);
}

static inline GListModel *
create_filter_model (GListModel *model,
                     GtkFilter  *filter)
{
  g_autoptr(GtkFilterListModel) filter_model = NULL;

  filter_model = gtk_filter_list_model_new (model, filter);
  gtk_filter_list_model_set_watch_items (filter_model, TRUE);
  gtk_filter_list_model_set_incremental (filter_model, FALSE);

  return G_LIST_MODEL (g_steal_pointer (&filter_model));
}

static void
publish_catalog (SysprofMarksSectionModel *self)
{
  g_autoptr(GtkExpression) expression = NULL;
  SysprofDocument *document;
  gint64 begin = SYSPROF_CAPTURE_CURRENT_TIME;

  g_assert (SYSPROF_IS_MARKS_SECTION_MODEL (self));
  g_assert (SYSPROF_IS_SESSION (self->session));

  document = sysprof_session_get_document (self->session);

#define MAP(_model) (G_LIST_MODEL (sysprof_expression_map_model_new (G_LIST_MODEL ((_model)), \
                                                                     GTK_EXPRESSION (gtk_closure_expression_new (G_TYPE_OBJECT, g_cclosure_new (G_CALLBACK (map_func), NULL, NULL), 0, NULL)))))
#define FLATTEN(_model) (G_LIST_MODEL (gtk_flatten_list_model_new (G_LIST_MODEL ((_model)))))
#define FILTER(_model, _filter) (create_filter_model (G_LIST_MODEL (_model), GTK_FILTER (_filter)))

  self->summary = _sysprof_mark_prepare_publish (self->prepared, document);
  self->groups = MAP (g_object_ref (self->summary));

  /* Turns groups into SysprofMarksSectionModelItem<SysprofMarkCatalogs> */
  self->catalog = MAP (FLATTEN (g_object_ref (self->groups)));

  /* Filter groups based on SysprofMarksSectionModelItem.visible */
  expression = gtk_property_expression_new (SYSPROF_TYPE_MARKS_SECTION_MODEL_ITEM, NULL, "visible");
  self->filtered_groups = FILTER (g_object_ref (self->groups), gtk_bool_filter_new (g_steal_pointer (&expression)));

  /* Filter SysprofMarksSectionModelItem<SysprofMarkCatalogs> based on the visible property */
  expression = gtk_property_expression_new (SYSPROF_TYPE_MARKS_SECTION_MODEL_ITEM, NULL, "visible");
  self->filtered_catalog = FILTER (g_object_ref (self->catalog), gtk_bool_filter_new (g_steal_pointer (&expression)));

  self->changed_handler =
    g_signal_connect_object (self->filtered_catalog,
                             "items-changed",
                             G_CALLBACK (queue_selection),
                             self,
                             G_CONNECT_SWAPPED);
  queue_selection (self);

#undef MAP
#undef FLATTEN
#undef FILTER

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILTERED_GROUPS]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILTERED_MARK_CATALOG]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILTERED_MARKS]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SUMMARY]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GROUPS]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MARK_CATALOG]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MARKS]);
  sysprof_collector_mark (begin,
                          SYSPROF_CAPTURE_CURRENT_TIME - begin,
                          "Sysprof",
                          "Publish mark catalog",
                          NULL);
}

static DexFuture *
preparation_ready (DexFuture *future,
                   gpointer   data)
{
  Completion *completion = data;
  g_autoptr(SysprofMarksSectionModel) self = g_weak_ref_get (&completion->model);
  g_autoptr(GError) error = NULL;
  const GValue *value;

  if (self == NULL || self->generation != completion->generation)
    return NULL;

  if ((value = dex_future_get_value (future, &error)))
    {
      self->prepared = g_value_dup_boxed (value);
      publish_catalog (self);
    }
  else if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
           !g_error_matches (error, DEX_ERROR, DEX_ERROR_FIBER_CANCELLED))
    {
      set_state (self, FALSE, error->message);
    }

  return NULL;
}

static void
sysprof_marks_section_model_connect (SysprofMarksSectionModel *self)
{
  SysprofDocument *document = sysprof_session_get_document (self->session);
  g_autoptr(SysprofDocumentStorage) storage = _sysprof_document_dup_storage (document);

  self->marks = sysprof_document_list_marks (document);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MARKS]);
  set_state (self, TRUE, NULL);
  self->preparation = dex_future_finally (_sysprof_mark_prepare (storage), preparation_ready,
                                          completion_new (self), completion_free);
}

void
sysprof_marks_section_model_retry (SysprofMarksSectionModel *self)
{
  g_return_if_fail (SYSPROF_IS_MARKS_SECTION_MODEL (self));

  if (self->session == NULL || self->loading)
    return;

  if (self->prepared)
    {
      queue_selection (self);
    }
  else
    {
      dex_clear (&self->preparation);
      g_clear_object (&self->marks);
      sysprof_marks_section_model_connect (self);
    }
}

static void
sysprof_marks_section_model_dispose (GObject *object)
{
  SysprofMarksSectionModel *self = (SysprofMarksSectionModel *)object;

  if (self->session)
    {
      sysprof_marks_section_model_disconnect (self);
      g_clear_object (&self->session);
    }

  G_OBJECT_CLASS (sysprof_marks_section_model_parent_class)->dispose (object);
}

static void
sysprof_marks_section_model_get_property (GObject    *object,
                                          guint       prop_id,
                                          GValue     *value,
                                          GParamSpec *pspec)
{
  SysprofMarksSectionModel *self = SYSPROF_MARKS_SECTION_MODEL (object);

  switch (prop_id)
    {
    case PROP_FILTERED_GROUPS:
      g_value_set_object (value, self->filtered_groups);
      break;

    case PROP_FILTERED_MARK_CATALOG:
      g_value_set_object (value, self->filtered_catalog);
      break;

    case PROP_FILTERED_MARKS:
      g_value_set_object (value, self->filtered_marks);
      break;

    case PROP_GROUPS:
      g_value_set_object (value, self->groups);
      break;

    case PROP_MARK_CATALOG:
      g_value_set_object (value, self->catalog);
      break;

    case PROP_MARKS:
      g_value_set_object (value, self->marks);
      break;

    case PROP_SUMMARY:
      g_value_set_object (value, self->summary);
      break;

    case PROP_LOADING:
      g_value_set_boolean (value, self->loading);
      break;

    case PROP_STATUS:
      g_value_set_string (value,
                          self->loading
                            ? (self->filtered_marks ? _("Updating marks…") : _("Preparing marks…"))
                            : self->error);
      break;

    case PROP_ERROR:
      g_value_set_boolean (value, self->error != NULL);
      break;

    case PROP_SESSION:
      g_value_set_object (value, sysprof_marks_section_model_get_session (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_marks_section_model_set_property (GObject      *object,
                                          guint         prop_id,
                                          const GValue *value,
                                          GParamSpec   *pspec)
{
  SysprofMarksSectionModel *self = SYSPROF_MARKS_SECTION_MODEL (object);

  switch (prop_id)
    {
    case PROP_SESSION:
      sysprof_marks_section_model_set_session (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_marks_section_model_class_init (SysprofMarksSectionModelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = sysprof_marks_section_model_dispose;
  object_class->get_property = sysprof_marks_section_model_get_property;
  object_class->set_property = sysprof_marks_section_model_set_property;

  properties[PROP_FILTERED_GROUPS] =
    g_param_spec_object ("filtered-groups", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_FILTERED_MARK_CATALOG] =
    g_param_spec_object ("filtered-mark-catalog", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_FILTERED_MARKS] =
    g_param_spec_object ("filtered-marks", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_GROUPS] =
    g_param_spec_object ("groups", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_MARK_CATALOG] =
    g_param_spec_object ("mark-catalog", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_MARKS] =
    g_param_spec_object ("marks", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_SESSION] =
    g_param_spec_object ("session", NULL, NULL,
                         SYSPROF_TYPE_SESSION,
                         (G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_SUMMARY] =
    g_param_spec_object ("summary", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_LOADING] =
    g_param_spec_boolean ("loading", NULL, NULL,
                          FALSE,
                          (G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS));

  properties[PROP_ERROR] =
    g_param_spec_boolean ("error", NULL, NULL,
                          FALSE,
                          (G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS));

  properties[PROP_STATUS] =
    g_param_spec_string ("status", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_marks_section_model_init (SysprofMarksSectionModel *self)
{
}

SysprofSession *
sysprof_marks_section_model_get_session (SysprofMarksSectionModel *self)
{
  g_return_val_if_fail (SYSPROF_IS_MARKS_SECTION_MODEL (self), NULL);

  return self->session;
}

void
sysprof_marks_section_model_set_session (SysprofMarksSectionModel *self,
                                         SysprofSession           *session)
{
  g_return_if_fail (SYSPROF_IS_MARKS_SECTION_MODEL (self));
  g_return_if_fail (!session || SYSPROF_IS_SESSION (session));

  if (self->session == session)
    return;

  if (self->session)
    sysprof_marks_section_model_disconnect (self);

  g_set_object (&self->session, session);

  if (session)
    sysprof_marks_section_model_connect (self);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SESSION]);
}

GListModel *
sysprof_marks_section_model_get_groups (SysprofMarksSectionModel *self)
{
  g_return_val_if_fail (SYSPROF_IS_MARKS_SECTION_MODEL (self), NULL);

  return self->groups;
}

GListModel *
sysprof_marks_section_model_get_filtered_groups (SysprofMarksSectionModel *self)
{
  g_return_val_if_fail (SYSPROF_IS_MARKS_SECTION_MODEL (self), NULL);

  return self->filtered_groups;
}

GListModel *
sysprof_marks_section_model_get_mark_catalog (SysprofMarksSectionModel *self)
{
  g_return_val_if_fail (SYSPROF_IS_MARKS_SECTION_MODEL (self), NULL);

  return self->catalog;
}

GListModel *
sysprof_marks_section_model_get_filtered_mark_catalog (SysprofMarksSectionModel *self)
{
  g_return_val_if_fail (SYSPROF_IS_MARKS_SECTION_MODEL (self), NULL);

  return self->filtered_catalog;
}

GListModel *
sysprof_marks_section_model_get_marks (SysprofMarksSectionModel *self)
{
  g_return_val_if_fail (SYSPROF_IS_MARKS_SECTION_MODEL (self), NULL);

  return self->marks;
}

GListModel *
sysprof_marks_section_model_get_filtered_marks (SysprofMarksSectionModel *self)
{
  g_return_val_if_fail (SYSPROF_IS_MARKS_SECTION_MODEL (self), NULL);

  return self->filtered_marks;
}
