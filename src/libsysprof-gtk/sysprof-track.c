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

typedef struct _SysprofTrackPrivate
{
  GListStore *subtracks;
  char *title;
} SysprofTrackPrivate;

enum {
  PROP_0,
  PROP_SUBTRACKS,
  PROP_TITLE,
  N_PROPS
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (SysprofTrack, sysprof_track, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static void
sysprof_track_dispose (GObject *object)
{
  SysprofTrack *self = (SysprofTrack *)object;
  SysprofTrackPrivate *priv = sysprof_track_get_instance_private (self);

  g_clear_object (&priv->subtracks);
  g_clear_pointer (&priv->title, g_free);

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
  SysprofTrackPrivate *priv = sysprof_track_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_TITLE:
      priv->title = g_value_dup_string (value);
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

  properties[PROP_SUBTRACKS] =
    g_param_spec_object ("subtracks", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_TITLE] =
    g_param_spec_string ("title", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_track_init (SysprofTrack *self)
{
  SysprofTrackPrivate *priv = sysprof_track_get_instance_private (self);

  priv->subtracks = g_list_store_new (SYSPROF_TYPE_TRACK);
}

const char *
sysprof_track_get_title (SysprofTrack *self)
{
  SysprofTrackPrivate *priv = sysprof_track_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_TRACK (self), NULL);

  return priv->title;
}

void
_sysprof_track_add_subtrack (SysprofTrack *self,
                             SysprofTrack *subtrack)
{
  SysprofTrackPrivate *priv = sysprof_track_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_TRACK (self));
  g_return_if_fail (SYSPROF_IS_TRACK (subtrack));
  g_return_if_fail (subtrack != self);

  g_list_store_append (priv->subtracks, subtrack);
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
  SysprofTrackPrivate *priv = sysprof_track_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_TRACK (self), NULL);

  return g_object_ref (G_LIST_MODEL (priv->subtracks));
}
