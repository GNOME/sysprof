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

#include "sysprof-track.h"

struct _SysprofTrack
{
  GObject parent_instance;
  char *title;
};

enum {
  PROP_0,
  PROP_TITLE,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofTrack, sysprof_track, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static void
sysprof_track_dispose (GObject *object)
{
  SysprofTrack *self = (SysprofTrack *)object;

  g_clear_pointer (&self->title, g_free);

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

  properties[PROP_TITLE] =
    g_param_spec_string ("title", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_track_init (SysprofTrack *self)
{
}

const char *
sysprof_track_get_title (SysprofTrack *self)
{
  g_return_val_if_fail (SYSPROF_IS_TRACK (self), NULL);

  return self->title;
}
