/* egg-handle.c
 *
 * Copyright 2021 Christian Hergert <chergert@redhat.com>
 *
 * This file is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.
 *
 * This file is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "config.h"

#include "egg-handle-private.h"

#define EXTRA_SIZE 8

struct _EggHandle
{
  GtkWidget        parent_instance;
  GtkWidget       *separator;
  GtkPositionType  position : 3;
};

G_DEFINE_TYPE (EggHandle, egg_handle, GTK_TYPE_WIDGET)

static gboolean
egg_handle_contains (GtkWidget *widget,
                     double     x,
                     double     y)
{
  EggHandle *self = (EggHandle *)widget;
  graphene_rect_t area;

  g_assert (EGG_IS_HANDLE (self));

  if (!gtk_widget_compute_bounds (GTK_WIDGET (self->separator),
                                  GTK_WIDGET (self),
                                  &area))
      return FALSE;

  switch (self->position)
    {
    case GTK_POS_LEFT:
      area.origin.x -= EXTRA_SIZE;
      area.size.width = EXTRA_SIZE;
      break;

    case GTK_POS_RIGHT:
      area.size.width = EXTRA_SIZE;
      break;

    case GTK_POS_TOP:
      area.origin.y -= EXTRA_SIZE;
      area.size.height = EXTRA_SIZE;
      break;

    case GTK_POS_BOTTOM:
      area.size.height = EXTRA_SIZE;
      break;

    default:
      g_assert_not_reached ();
      break;
    }

  return graphene_rect_contains_point (&area, &GRAPHENE_POINT_INIT (x, y));
}

static void
egg_handle_dispose (GObject *object)
{
  EggHandle *self = (EggHandle *)object;

  g_clear_pointer (&self->separator, gtk_widget_unparent);

  G_OBJECT_CLASS (egg_handle_parent_class)->dispose (object);
}

static void
egg_handle_class_init (EggHandleClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = egg_handle_dispose;

  widget_class->contains = egg_handle_contains;

  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
}

static void
egg_handle_init (EggHandle *self)
{
  self->separator = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_parent (GTK_WIDGET (self->separator), GTK_WIDGET (self));
}

void
egg_handle_set_position (EggHandle       *self,
                         GtkPositionType  position)
{
  g_return_if_fail (EGG_IS_HANDLE (self));

  self->position = position;

  switch (position)
    {
    case GTK_POS_LEFT:
    case GTK_POS_RIGHT:
      gtk_widget_set_cursor_from_name (GTK_WIDGET (self), "col-resize");
      gtk_orientable_set_orientation (GTK_ORIENTABLE (self->separator), GTK_ORIENTATION_VERTICAL);
      break;

    case GTK_POS_TOP:
    case GTK_POS_BOTTOM:
      gtk_widget_set_cursor_from_name (GTK_WIDGET (self), "row-resize");
      gtk_orientable_set_orientation (GTK_ORIENTABLE (self->separator), GTK_ORIENTATION_HORIZONTAL);
      break;

    default:
      g_assert_not_reached ();
    }
}

GtkWidget *
egg_handle_new (GtkPositionType position)
{
  EggHandle *self;

  self = g_object_new (EGG_TYPE_HANDLE, NULL);
  egg_handle_set_position (self, position);

  return GTK_WIDGET (self);
}
