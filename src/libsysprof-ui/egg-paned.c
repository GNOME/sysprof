/* egg-paned.c
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

#include <string.h>

#include "egg-paned-private.h"
#include "egg-resizer-private.h"

struct _EggPaned
{
  GtkWidget      parent_instance;
  GtkOrientation orientation;
};

static void buildable_iface_init (GtkBuildableIface *iface);

G_DEFINE_TYPE_WITH_CODE (EggPaned, egg_paned, GTK_TYPE_WIDGET,
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, buildable_iface_init)
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_ORIENTABLE, NULL))

enum {
  PROP_0,
  N_PROPS,

  PROP_ORIENTATION,
};

static void
update_orientation (GtkWidget      *widget,
                    GtkOrientation  orientation)
{
  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    {
      gtk_widget_remove_css_class (widget, "vertical");
      gtk_widget_add_css_class (widget, "horizontal");
    }
  else
    {
      gtk_widget_remove_css_class (widget, "horizontal");
      gtk_widget_add_css_class (widget, "vertical");
    }

  gtk_accessible_update_property (GTK_ACCESSIBLE (widget),
                                  GTK_ACCESSIBLE_PROPERTY_ORIENTATION, orientation,
                                  -1);
}

/**
 * egg_paned_new:
 *
 * Create a new #EggPaned.
 *
 * Returns: (transfer full): a newly created #EggPaned
 */
GtkWidget *
egg_paned_new (void)
{
  return g_object_new (EGG_TYPE_PANED, NULL);
}

static void
egg_paned_set_orientation (EggPaned       *self,
                           GtkOrientation  orientation)
{
  GtkPositionType pos;

  g_assert (EGG_IS_PANED (self));
  g_assert (orientation == GTK_ORIENTATION_HORIZONTAL ||
            orientation == GTK_ORIENTATION_VERTICAL);

  if (self->orientation == orientation)
    return;

  self->orientation = orientation;

  if (self->orientation == GTK_ORIENTATION_HORIZONTAL)
    pos = GTK_POS_LEFT;
  else
    pos = GTK_POS_TOP;

  for (GtkWidget *child = gtk_widget_get_last_child (GTK_WIDGET (self));
       child != NULL;
       child = gtk_widget_get_prev_sibling (child))
    {
      g_assert (EGG_IS_RESIZER (child));

      egg_resizer_set_position (EGG_RESIZER (child), pos);
    }

  update_orientation (GTK_WIDGET (self), self->orientation);
  gtk_widget_queue_resize (GTK_WIDGET (self));
  g_object_notify (G_OBJECT (self), "orientation");
}

static void
egg_paned_measure (GtkWidget      *widget,
                   GtkOrientation  orientation,
                   int             for_size,
                   int            *minimum,
                   int            *natural,
                   int            *minimum_baseline,
                   int            *natural_baseline)
{
  EggPaned *self = (EggPaned *)widget;

  g_assert (EGG_IS_PANED (self));

  *minimum = 0;
  *natural = 0;
  *minimum_baseline = -1;
  *natural_baseline = -1;

  for (GtkWidget *child = gtk_widget_get_first_child (widget);
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      int child_min, child_nat;

      gtk_widget_measure (child, orientation, for_size, &child_min, &child_nat, NULL, NULL);

      if (orientation == self->orientation)
        {
          *minimum += child_min;
          *natural += child_nat;
        }
      else
        {
          *minimum = MAX (*minimum, child_min);
          *natural = MAX (*natural, child_nat);
        }
    }
}

typedef struct
{
  GtkWidget      *widget;
  GtkRequisition  min_request;
  GtkRequisition  nat_request;
  GtkAllocation   alloc;
} ChildAllocation;

static void
egg_paned_size_allocate (GtkWidget *widget,
                         int        width,
                         int        height,
                         int        baseline)
{
  EggPaned *self = (EggPaned *)widget;
  ChildAllocation *allocs;
  ChildAllocation *last_alloc = NULL;
  GtkOrientation orientation;
  guint n_children = 0;
  guint n_expand = 0;
  guint i;
  int extra_width = width;
  int extra_height = height;
  int expand_width;
  int expand_height;
  int x, y;

  g_assert (EGG_IS_PANED (self));

  GTK_WIDGET_CLASS (egg_paned_parent_class)->size_allocate (widget, width, height, baseline);

  n_children = egg_paned_get_n_children (self);

  if (n_children == 1)
    {
      GtkWidget *child = gtk_widget_get_first_child (widget);
      GtkAllocation alloc = { 0, 0, width, height };

      if (gtk_widget_get_visible (child))
        {
          gtk_widget_size_allocate (child, &alloc, -1);
          return;
        }
    }

  orientation = gtk_orientable_get_orientation (GTK_ORIENTABLE (self));
  allocs = g_newa (ChildAllocation, n_children);
  memset (allocs, 0, sizeof *allocs * n_children);

  /* Give min size to each of the children */
  i = 0;
  for (GtkWidget *child = gtk_widget_get_first_child (GTK_WIDGET (self));
       child != NULL;
       child = gtk_widget_get_next_sibling (child), i++)
    {
      ChildAllocation *child_alloc = &allocs[i];

      if (!gtk_widget_get_visible (child))
        continue;

      gtk_widget_measure (child, GTK_ORIENTATION_HORIZONTAL, height,
                          &child_alloc->min_request.width,
                          &child_alloc->nat_request.width,
                          NULL, NULL);
      gtk_widget_measure (child, GTK_ORIENTATION_VERTICAL, width,
                          &child_alloc->min_request.height,
                          &child_alloc->nat_request.height,
                          NULL, NULL);

      child_alloc->alloc.width = child_alloc->min_request.width;
      child_alloc->alloc.height = child_alloc->min_request.height;

      n_expand += gtk_widget_compute_expand (child, orientation);

      if (orientation == GTK_ORIENTATION_HORIZONTAL)
        {
          extra_width -= child_alloc->alloc.width;
          child_alloc->alloc.height = height;
        }
      else
        {
          extra_height -= child_alloc->alloc.height;
          child_alloc->alloc.width = width;
        }
    }

  /* Now try to distribute extra space for natural size */
  i = 0;
  for (GtkWidget *child = gtk_widget_get_first_child (GTK_WIDGET (self));
       child != NULL;
       child = gtk_widget_get_next_sibling (child), i++)
    {
      ChildAllocation *child_alloc = &allocs[i];

      if (!gtk_widget_get_visible (child))
        continue;

      if (orientation == GTK_ORIENTATION_HORIZONTAL)
        {
          int taken = MIN (extra_width, child_alloc->nat_request.width - child_alloc->alloc.width);

          if (taken > 0)
            {
              child_alloc->alloc.width += taken;
              extra_width -= taken;
            }
        }
      else
        {
          int taken = MIN (extra_height, child_alloc->nat_request.height - child_alloc->alloc.height);

          if (taken > 0)
            {
              child_alloc->alloc.height += taken;
              extra_height -= taken;
            }
        }

      last_alloc = child_alloc;
    }

  /* Now give extra space for those that expand */
  expand_width = n_expand ? extra_width / n_expand : 0;
  expand_height = n_expand ? extra_height / n_expand : 0;
  i = n_children;
  for (GtkWidget *child = gtk_widget_get_last_child (GTK_WIDGET (self));
       child != NULL;
       child = gtk_widget_get_prev_sibling (child), i--)
    {
      ChildAllocation *child_alloc = &allocs[i-1];

      if (!gtk_widget_get_visible (child))
        continue;

      if (!gtk_widget_compute_expand (child, orientation))
        continue;

      if (orientation == GTK_ORIENTATION_HORIZONTAL)
        {
          child_alloc->alloc.width += expand_width;
          extra_width -= expand_width;
        }
      else
        {
          child_alloc->alloc.height += expand_height;
          extra_height -= expand_height;
        }
    }

  /* Give any leftover to the last visible child */
  if (last_alloc)
    {
      if (orientation == GTK_ORIENTATION_HORIZONTAL)
        last_alloc->alloc.width += extra_width;
      else
        last_alloc->alloc.height += extra_height;
    }

  i = 0;
  x = 0;
  y = 0;
  for (GtkWidget *child = gtk_widget_get_first_child (GTK_WIDGET (self));
       child != NULL;
       child = gtk_widget_get_next_sibling (child), i++)
    {
      ChildAllocation *child_alloc = &allocs[i];

      child_alloc->alloc.x = x;
      child_alloc->alloc.y = y;

      if (orientation == GTK_ORIENTATION_HORIZONTAL)
        x += child_alloc->alloc.width;
      else
        y += child_alloc->alloc.height;

      gtk_widget_size_allocate (child, &child_alloc->alloc, -1);
    }
}

static void
egg_paned_dispose (GObject *object)
{
  EggPaned *self = (EggPaned *)object;
  GtkWidget *child;

  child = gtk_widget_get_first_child (GTK_WIDGET (self));
  while (child)
    {
      GtkWidget *next = gtk_widget_get_next_sibling (child);
      gtk_widget_unparent (child);
      child = next;
    }

  G_OBJECT_CLASS (egg_paned_parent_class)->dispose (object);
}

static void
egg_paned_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
  EggPaned *self = EGG_PANED (object);

  switch (prop_id)
    {
    case PROP_ORIENTATION:
      g_value_set_enum (value, self->orientation);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
egg_paned_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
  EggPaned *self = EGG_PANED (object);

  switch (prop_id)
    {
    case PROP_ORIENTATION:
      egg_paned_set_orientation (self, g_value_get_enum (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
egg_paned_class_init (EggPanedClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = egg_paned_dispose;
  object_class->get_property = egg_paned_get_property;
  object_class->set_property = egg_paned_set_property;

  widget_class->measure = egg_paned_measure;
  widget_class->size_allocate = egg_paned_size_allocate;

  g_object_class_override_property (object_class, PROP_ORIENTATION, "orientation");

  gtk_widget_class_set_css_name (widget_class, "eggpaned");
}

static void
egg_paned_init (EggPaned *self)
{
  self->orientation = GTK_ORIENTATION_HORIZONTAL;

  update_orientation (GTK_WIDGET (self), self->orientation);
}

static void
egg_paned_update_handles (EggPaned *self)
{
  GtkWidget *child;

  g_assert (EGG_IS_PANED (self));

  for (child = gtk_widget_get_first_child (GTK_WIDGET (self));
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      GtkWidget *handle;

      g_assert (EGG_IS_RESIZER (child));

      if ((handle = egg_resizer_get_handle (EGG_RESIZER (child))))
        gtk_widget_show (handle);
    }

  if ((child = gtk_widget_get_last_child (GTK_WIDGET (self))))
    {
      GtkWidget *handle = egg_resizer_get_handle (EGG_RESIZER (child));
      gtk_widget_hide (handle);
    }
}

void
egg_paned_remove (EggPaned  *self,
                  GtkWidget *child)
{
  GtkWidget *resizer;

  g_return_if_fail (EGG_IS_PANED (self));
  g_return_if_fail (GTK_IS_WIDGET (child));

  resizer = gtk_widget_get_ancestor (child, EGG_TYPE_RESIZER);
  g_return_if_fail (resizer != NULL &&
                    gtk_widget_get_parent (resizer) == GTK_WIDGET (self));
  gtk_widget_unparent (resizer);
  egg_paned_update_handles (self);
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

void
egg_paned_insert (EggPaned  *self,
                  int        position,
                  GtkWidget *child)
{
  GtkPositionType pos;
  GtkWidget *resizer;

  g_return_if_fail (EGG_IS_PANED (self));
  g_return_if_fail (GTK_IS_WIDGET (child));
  g_return_if_fail (gtk_widget_get_parent (child) == NULL);

  if (self->orientation == GTK_ORIENTATION_HORIZONTAL)
    pos = GTK_POS_LEFT;
  else
    pos = GTK_POS_TOP;

  resizer = egg_resizer_new (pos);
  egg_resizer_set_child (EGG_RESIZER (resizer), child);

  if (position < 0)
    gtk_widget_insert_before (GTK_WIDGET (resizer), GTK_WIDGET (self), NULL);
  else if (position == 0)
    gtk_widget_insert_after (GTK_WIDGET (resizer), GTK_WIDGET (self), NULL);
  else
    {
      GtkWidget *sibling = gtk_widget_get_first_child (GTK_WIDGET (self));

      for (int i = position; i > 0 && sibling != NULL; i--)
        sibling = gtk_widget_get_next_sibling (sibling);

      gtk_widget_insert_before (GTK_WIDGET (resizer), GTK_WIDGET (self), sibling);
    }

  egg_paned_update_handles (self);

  gtk_widget_queue_resize (GTK_WIDGET (self));
}

void
egg_paned_append (EggPaned  *self,
                  GtkWidget *child)
{
  egg_paned_insert (self, -1, child);
}

void
egg_paned_prepend (EggPaned  *self,
                   GtkWidget *child)
{
  egg_paned_insert (self, 0, child);
}

void
egg_paned_insert_after (EggPaned  *self,
                        GtkWidget *child,
                        GtkWidget *sibling)
{
  int position = 0;

  g_return_if_fail (EGG_IS_PANED (self));
  g_return_if_fail (GTK_IS_WIDGET (child));
  g_return_if_fail (!sibling || GTK_IS_WIDGET (sibling));

  if (sibling == NULL)
    {
      egg_paned_prepend (self, child);
      return;
    }

  /* TODO: We should reverse insert() to call this */

  for (GtkWidget *ancestor = gtk_widget_get_first_child (GTK_WIDGET (self));
       ancestor != NULL;
       ancestor = gtk_widget_get_next_sibling (ancestor))
    {
      position++;

      if (sibling == ancestor || gtk_widget_is_ancestor (sibling, ancestor))
        break;
    }

  egg_paned_insert (self, position, child);
}

guint
egg_paned_get_n_children (EggPaned *self)
{
  guint count = 0;

  for (GtkWidget *child = gtk_widget_get_first_child (GTK_WIDGET (self));
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    count++;

  return count;
}

GtkWidget *
egg_paned_get_nth_child (EggPaned *self,
                         guint     nth)
{
  g_return_val_if_fail (EGG_IS_PANED (self), NULL);

  for (GtkWidget *child = gtk_widget_get_first_child (GTK_WIDGET (self));
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      g_assert (EGG_IS_RESIZER (child));

      if (nth == 0)
        return egg_resizer_get_child (EGG_RESIZER (child));

      nth--;
    }

  return NULL;
}

static void
egg_paned_add_child (GtkBuildable *buildable,
                     GtkBuilder   *builder,
                     GObject      *child,
                     const char   *type)
{
  if (GTK_IS_WIDGET (child))
    egg_paned_append (EGG_PANED (buildable), GTK_WIDGET (child));
  else
    g_warning ("Cannot add child of type %s to %s",
               G_OBJECT_TYPE_NAME (child),
               G_OBJECT_TYPE_NAME (buildable));
}

static void
buildable_iface_init (GtkBuildableIface *iface)
{
  iface->add_child = egg_paned_add_child;
}
