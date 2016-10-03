/* sp-viewport.c
 *
 * Copyright (C) 2016 Christian Hergert <chergert@redhat.com>
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
 */

/*
 * This is meant to act like GtkViewport but allow us to override the
 * GtkAdjustment without having viewport change our values.
 */

#define G_LOG_DOMAIN "sp-viewport"

#include "sp-viewport.h"

struct _SpViewport
{
  GtkViewport parent_instance;

  GtkAdjustment *hadjustment;
  GtkAdjustment *vadjustment;
};

G_DEFINE_TYPE (SpViewport, sp_viewport, GTK_TYPE_VIEWPORT)

enum {
  PROP_0,
  N_PROPS,

  /* Overridden properties */
  PROP_HADJUSTMENT,
  PROP_VADJUSTMENT,
};

static void
sp_viewport_finalize (GObject *object)
{
  SpViewport *self = (SpViewport *)object;

  g_clear_object (&self->hadjustment);
  g_clear_object (&self->vadjustment);

  G_OBJECT_CLASS (sp_viewport_parent_class)->finalize (object);
}

static void
sp_viewport_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  SpViewport *self = SP_VIEWPORT (object);

  switch (prop_id)
    {
    case PROP_HADJUSTMENT:
      g_value_set_object (value, self->hadjustment);
      break;

    case PROP_VADJUSTMENT:
      g_value_set_object (value, self->vadjustment);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_viewport_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  SpViewport *self = SP_VIEWPORT (object);

  switch (prop_id)
    {
    case PROP_HADJUSTMENT:
      g_clear_object (&self->hadjustment);
      self->hadjustment = g_value_dup_object (value);
      g_object_notify_by_pspec (object, pspec);
      break;

    case PROP_VADJUSTMENT:
      g_clear_object (&self->vadjustment);
      self->vadjustment = g_value_dup_object (value);
      g_object_notify_by_pspec (object, pspec);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_viewport_class_init (SpViewportClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = sp_viewport_set_property;
  object_class->get_property = sp_viewport_get_property;
  object_class->finalize = sp_viewport_finalize;

  g_object_class_override_property (object_class, PROP_HADJUSTMENT, "hadjustment");
  g_object_class_override_property (object_class, PROP_VADJUSTMENT, "vadjustment");
}

static void
sp_viewport_init (SpViewport *self)
{
  self->hadjustment = gtk_adjustment_new (0, 0, 0, 0, 0, 0);
  self->vadjustment = gtk_adjustment_new (0, 0, 0, 0, 0, 0);
}
