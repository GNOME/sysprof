/* egg-resizer.c
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
#include "egg-resizer-private.h"

#define HANDLE_SIZE 8

struct _EggResizer
{
  GtkWidget        parent_instance;

  EggHandle       *handle;
  GtkWidget       *child;

  double           drag_orig_size;
  double           drag_position;

  GtkPositionType  position : 3;
};

G_DEFINE_TYPE (EggResizer, egg_resizer, GTK_TYPE_WIDGET)

enum {
  PROP_0,
  PROP_CHILD,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

static void
egg_resizer_drag_begin_cb (EggResizer   *self,
                             double          start_x,
                             double          start_y,
                             GtkGestureDrag *drag)
{
  GtkAllocation child_alloc;
  GtkAllocation handle_alloc;

  g_assert (EGG_IS_RESIZER (self));
  g_assert (GTK_IS_GESTURE_DRAG (drag));

  if (self->child == NULL)
    return;

  switch (self->position)
    {
    case GTK_POS_LEFT:
      if (start_x > gtk_widget_get_width (GTK_WIDGET (self)) - HANDLE_SIZE)
        goto start_drag;
      break;

    case GTK_POS_RIGHT:
      if (start_x <= HANDLE_SIZE)
        goto start_drag;
      break;

    case GTK_POS_TOP:
      if (start_y > gtk_widget_get_height (GTK_WIDGET (self)) - HANDLE_SIZE)
        goto start_drag;
      break;

    case GTK_POS_BOTTOM:
      if (start_y <= HANDLE_SIZE)
        goto start_drag;
      break;

    default:
      g_assert_not_reached ();
      break;
    }

  gtk_gesture_set_state (GTK_GESTURE (drag),
                         GTK_EVENT_SEQUENCE_DENIED);

  return;

start_drag:

  gtk_widget_get_allocation (self->child, &child_alloc);
  gtk_widget_get_allocation (GTK_WIDGET (self->handle), &handle_alloc);

  if (self->position == GTK_POS_LEFT ||
      self->position == GTK_POS_RIGHT)
    {
      self->drag_orig_size = child_alloc.width + handle_alloc.width;
      gtk_widget_set_hexpand (self->child, FALSE);
    }
  else
    {
      self->drag_orig_size = child_alloc.height + handle_alloc.height;
      gtk_widget_set_vexpand (self->child, FALSE);
    }

  self->drag_position = self->drag_orig_size;

  gtk_widget_queue_resize (GTK_WIDGET (self));
}

static void
egg_resizer_drag_update_cb (EggResizer   *self,
                              double          offset_x,
                              double          offset_y,
                              GtkGestureDrag *drag)
{
  g_assert (EGG_IS_RESIZER (self));
  g_assert (GTK_IS_GESTURE_DRAG (drag));

  if (self->position == GTK_POS_LEFT)
    self->drag_position = self->drag_orig_size + offset_x;
  else if (self->position == GTK_POS_RIGHT)
    self->drag_position = gtk_widget_get_width (GTK_WIDGET (self)) - offset_x;
  else if (self->position == GTK_POS_TOP)
    self->drag_position = self->drag_orig_size + offset_y;
  else if (self->position == GTK_POS_BOTTOM)
    self->drag_position = gtk_widget_get_height (GTK_WIDGET (self)) - offset_y;

  gtk_widget_queue_resize (GTK_WIDGET (self));
}

static void
egg_resizer_drag_end_cb (EggResizer   *self,
                           double          offset_x,
                           double          offset_y,
                           GtkGestureDrag *drag)
{
  g_assert (EGG_IS_RESIZER (self));
  g_assert (GTK_IS_GESTURE_DRAG (drag));
}

GtkWidget *
egg_resizer_new (GtkPositionType position)
{
  EggResizer *self;

  self = g_object_new (EGG_TYPE_RESIZER, NULL);
  self->position = position;
  self->handle = EGG_HANDLE (egg_handle_new (position));
  gtk_widget_set_parent (GTK_WIDGET (self->handle), GTK_WIDGET (self));

  return GTK_WIDGET (self);
}

static void
egg_resizer_measure (GtkWidget      *widget,
                       GtkOrientation  orientation,
                       int             for_size,
                       int            *minimum,
                       int            *natural,
                       int            *minimum_baseline,
                       int            *natural_baseline)
{
  EggResizer *self = (EggResizer *)widget;

  g_assert (EGG_IS_RESIZER (self));

  *minimum = 0;
  *natural = 0;
  *minimum_baseline = -1;
  *natural_baseline = -1;

  if (self->child != NULL)
    gtk_widget_measure (self->child,
                        orientation,
                        for_size,
                        minimum, natural,
                        NULL, NULL);

  if ((orientation == GTK_ORIENTATION_HORIZONTAL &&
       (self->position == GTK_POS_LEFT ||
        self->position == GTK_POS_RIGHT)) ||
      (orientation == GTK_ORIENTATION_VERTICAL &&
       (self->position == GTK_POS_TOP ||
        self->position == GTK_POS_BOTTOM)))
    {
      int handle_min, handle_nat;

      if (self->drag_position > *minimum)
        *natural = self->drag_position;
      else if (self->drag_position < *minimum)
        *natural = *minimum;

      if (gtk_widget_get_visible (GTK_WIDGET (self->handle)))
        {
          gtk_widget_measure (GTK_WIDGET (self->handle),
                              orientation, for_size,
                              &handle_min, &handle_nat,
                              NULL, NULL);

          *minimum += handle_min;
          *natural += handle_nat;
        }
    }
}

static void
egg_resizer_size_allocate (GtkWidget *widget,
                             int        width,
                             int        height,
                             int        baseline)
{
  EggResizer *self = (EggResizer *)widget;
  GtkOrientation orientation;
  GtkAllocation child_alloc;
  GtkAllocation handle_alloc;
  int handle_min = 0, handle_nat = 0;

  g_assert (EGG_IS_RESIZER (self));

  if (self->position == GTK_POS_LEFT ||
      self->position == GTK_POS_RIGHT)
    orientation = GTK_ORIENTATION_HORIZONTAL;
  else
    orientation = GTK_ORIENTATION_VERTICAL;

  if (gtk_widget_get_visible (GTK_WIDGET (self->handle)))
    gtk_widget_measure (GTK_WIDGET (self->handle),
                        orientation,
                        -1,
                        &handle_min, &handle_nat,
                        NULL, NULL);

  switch (self->position)
    {
    case GTK_POS_LEFT:
      handle_alloc.x = width - handle_min;
      handle_alloc.width = handle_min;
      handle_alloc.y = 0;
      handle_alloc.height = height;
      child_alloc.x = 0;
      child_alloc.y = 0;
      child_alloc.width = width - handle_min;
      child_alloc.height = height;
      break;

    case GTK_POS_RIGHT:
      handle_alloc.x = 0;
      handle_alloc.width = handle_min;
      handle_alloc.y = 0;
      handle_alloc.height = height;
      child_alloc.x = handle_min;
      child_alloc.y = 0;
      child_alloc.width = width - handle_min;
      child_alloc.height = height;
      break;

    case GTK_POS_TOP:
      handle_alloc.x = 0;
      handle_alloc.width = width;
      handle_alloc.y = height - handle_min;
      handle_alloc.height = handle_min;
      child_alloc.x = 0;
      child_alloc.y = 0;
      child_alloc.width = width;
      child_alloc.height = height - handle_min;
      break;

    case GTK_POS_BOTTOM:
      handle_alloc.x = 0;
      handle_alloc.width = width;
      handle_alloc.y = 0;
      handle_alloc.height = handle_min;
      child_alloc.x = 0;
      child_alloc.y = handle_min;
      child_alloc.width = width;
      child_alloc.height = height - handle_min;
      break;

    default:
      g_assert_not_reached ();
    }

  if (gtk_widget_get_mapped (GTK_WIDGET (self->handle)))
    gtk_widget_size_allocate (GTK_WIDGET (self->handle), &handle_alloc, -1);

  if (self->child != NULL &&
      gtk_widget_get_mapped (self->child))
    gtk_widget_size_allocate (self->child, &child_alloc, -1);
}

static void
egg_resizer_compute_expand (GtkWidget *widget,
                              gboolean  *hexpand,
                              gboolean  *vexpand)
{
  EggResizer *self = EGG_RESIZER (widget);

  if (self->child != NULL)
    {
      *hexpand = gtk_widget_compute_expand (self->child, GTK_ORIENTATION_HORIZONTAL);
      *vexpand = gtk_widget_compute_expand (self->child, GTK_ORIENTATION_VERTICAL);
    }
  else
    {
      *hexpand = FALSE;
      *vexpand = FALSE;
    }
}

static void
egg_resizer_dispose (GObject *object)
{
  EggResizer *self = (EggResizer *)object;

  if (self->handle)
    gtk_widget_unparent (GTK_WIDGET (self->handle));
  self->handle = NULL;

  if (self->child)
    gtk_widget_unparent (self->child);
  self->child = NULL;

  G_OBJECT_CLASS (egg_resizer_parent_class)->dispose (object);
}

static void
egg_resizer_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  EggResizer *self = EGG_RESIZER (object);

  switch (prop_id)
    {
    case PROP_CHILD:
      g_value_set_object (value, egg_resizer_get_child (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
egg_resizer_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  EggResizer *self = EGG_RESIZER (object);

  switch (prop_id)
    {
    case PROP_CHILD:
      egg_resizer_set_child (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
egg_resizer_class_init (EggResizerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = egg_resizer_dispose;
  object_class->get_property = egg_resizer_get_property;
  object_class->set_property = egg_resizer_set_property;

  widget_class->compute_expand = egg_resizer_compute_expand;
  widget_class->measure = egg_resizer_measure;
  widget_class->size_allocate = egg_resizer_size_allocate;

  properties [PROP_CHILD] =
    g_param_spec_object ("child",
                         "Child",
                         "Child",
                         GTK_TYPE_WIDGET,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_css_name (widget_class, "eggresizer");
}

static void
egg_resizer_init (EggResizer *self)
{
  GtkGesture *gesture;

  gesture = gtk_gesture_drag_new ();
  gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (gesture), GTK_PHASE_CAPTURE);
  g_signal_connect_object (gesture,
                           "drag-begin",
                           G_CALLBACK (egg_resizer_drag_begin_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (gesture,
                           "drag-update",
                           G_CALLBACK (egg_resizer_drag_update_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (gesture,
                           "drag-end",
                           G_CALLBACK (egg_resizer_drag_end_cb),
                           self,
                           G_CONNECT_SWAPPED);
  gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (gesture));
}

/**
 * egg_resizer_get_child:
 * @self: a #EggResizer
 *
 * Gets the child widget of the resizer.
 *
 * Returns: (transfer none) (nullable): A #GtkWidget or %NULL
 */
GtkWidget *
egg_resizer_get_child (EggResizer *self)
{
  g_return_val_if_fail (EGG_IS_RESIZER (self), NULL);

  return self->child;
}

void
egg_resizer_set_child (EggResizer *self,
                         GtkWidget    *child)
{
  g_return_if_fail (EGG_IS_RESIZER (self));
  g_return_if_fail (!child || GTK_IS_WIDGET (child));

  if (child == self->child)
    return;

  g_clear_pointer (&self->child, gtk_widget_unparent);

  self->child = child;

  if (self->child != NULL)
    gtk_widget_insert_before (self->child,
                              GTK_WIDGET (self),
                              GTK_WIDGET (self->handle));

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CHILD]);
}

GtkPositionType
egg_resizer_get_position (EggResizer *self)
{
  g_return_val_if_fail (EGG_IS_RESIZER (self), 0);

  return self->position;
}

void
egg_resizer_set_position (EggResizer      *self,
                          GtkPositionType  position)
{
  g_return_if_fail (EGG_IS_RESIZER (self));

  if (position != self->position)
    {
      self->position = position;

      egg_handle_set_position (self->handle, position);
      gtk_widget_queue_resize (GTK_WIDGET (self));
    }
}

GtkWidget *
egg_resizer_get_handle (EggResizer *self)
{
  g_return_val_if_fail (EGG_IS_RESIZER (self), NULL);

  return GTK_WIDGET (self->handle);
}
