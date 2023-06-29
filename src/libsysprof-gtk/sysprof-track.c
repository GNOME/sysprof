/* sysprof-track.c
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

#include "sysprof-track-private.h"

struct _SysprofTrack
{
  GObject         parent_instance;
  SysprofSession *session;
  char           *title;
  GListStore     *subtracks;
  GMenuModel     *menu_model;
};

enum {
  PROP_0,
  PROP_MENU_MODEL,
  PROP_SESSION,
  PROP_SUBTRACKS,
  PROP_TITLE,
  N_PROPS
};

enum {
  CREATE_CHART,
  N_SIGNALS
};

G_DEFINE_FINAL_TYPE (SysprofTrack, sysprof_track, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];
static guint signals[N_SIGNALS];

static void
sysprof_track_dispose (GObject *object)
{
  SysprofTrack *self = (SysprofTrack *)object;

  g_clear_object (&self->menu_model);
  g_clear_object (&self->subtracks);
  g_clear_pointer (&self->title, g_free);
  g_clear_weak_pointer (&self->session);

  G_OBJECT_CLASS (sysprof_track_parent_class)->dispose (object);
}

static void
sysprof_track_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  SysprofTrack *self = SYSPROF_TRACK (object);

  switch (prop_id)
    {
    case PROP_MENU_MODEL:
      g_value_set_object (value, sysprof_track_get_menu_model (self));
      break;

    case PROP_SESSION:
      g_value_set_object (value, sysprof_track_get_session (self));
      break;

    case PROP_SUBTRACKS:
      g_value_take_object (value, sysprof_track_list_subtracks (self));
      break;

    case PROP_TITLE:
      g_value_set_string (value, sysprof_track_get_title (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_track_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  SysprofTrack *self = SYSPROF_TRACK (object);

  switch (prop_id)
    {
    case PROP_MENU_MODEL:
      self->menu_model = g_value_dup_object (value);
      break;

    case PROP_SESSION:
      g_set_weak_pointer (&self->session, g_value_get_object (value));
      break;

    case PROP_TITLE:
      self->title = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_track_class_init (SysprofTrackClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = sysprof_track_dispose;
  object_class->get_property = sysprof_track_get_property;
  object_class->set_property = sysprof_track_set_property;

  properties [PROP_MENU_MODEL] =
    g_param_spec_object ("menu-model", NULL, NULL,
                         G_TYPE_MENU_MODEL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_SESSION] =
    g_param_spec_object ("session", NULL, NULL,
                         SYSPROF_TYPE_SESSION,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties[PROP_SUBTRACKS] =
    g_param_spec_object ("subtracks", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_TITLE] =
    g_param_spec_string ("title", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  signals[CREATE_CHART] =
    g_signal_new ("create-chart",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  g_signal_accumulator_first_wins, NULL,
                  NULL,
                  GTK_TYPE_WIDGET, 0);
}

static void
sysprof_track_init (SysprofTrack *self)
{
  self->subtracks = g_list_store_new (SYSPROF_TYPE_TRACK);
}

const char *
sysprof_track_get_title (SysprofTrack *self)
{
  g_return_val_if_fail (SYSPROF_IS_TRACK (self), NULL);

  return self->title;
}

void
_sysprof_track_add_subtrack (SysprofTrack *self,
                             SysprofTrack *subtrack)
{
  g_return_if_fail (SYSPROF_IS_TRACK (self));
  g_return_if_fail (SYSPROF_IS_TRACK (subtrack));
  g_return_if_fail (subtrack != self);

  g_list_store_append (self->subtracks, subtrack);
}

/**
 * sysprof_track_list_subtracks:
 * @self: a #SysprofTrack
 *
 * Returns: (transfer full) (nullable): a #GListModel of #SysprofTrack
 */
GListModel *
sysprof_track_list_subtracks (SysprofTrack *self)
{
  g_return_val_if_fail (SYSPROF_IS_TRACK (self), NULL);

  if (g_list_model_get_n_items (G_LIST_MODEL (self->subtracks)) == 0)
    return NULL;

  return g_object_ref (G_LIST_MODEL (self->subtracks));
}

/**
 * sysprof_track_get_session:
 * @self: a #SysprofTrack
 *
 * Returns: (transfer none) (nullable): a #SysprofSession or %NULL
 */
SysprofSession *
sysprof_track_get_session (SysprofTrack *self)
{
  g_return_val_if_fail (SYSPROF_IS_TRACK (self), NULL);

  return self->session;
}

/**
 * sysprof_track_get_menu_model:
 * @self: a #SysprofTrack
 *
 * Gets the optional menu model for a track.
 *
 * Returns: (transfer none) (nullable): a #GMenuModel or %NULL
 */
GMenuModel *
sysprof_track_get_menu_model (SysprofTrack *self)
{
  g_return_val_if_fail (SYSPROF_IS_TRACK (self), NULL);

  return self->menu_model;
}

GtkWidget *
_sysprof_track_create_chart (SysprofTrack *self)
{
  GtkWidget *ret = NULL;

  g_return_val_if_fail (SYSPROF_IS_TRACK (self), NULL);

  g_signal_emit (self, signals[CREATE_CHART], 0, &ret);

  return ret;
}
