/* sysprof-progress-cell.c
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

#include <adwaita.h>

#include "sysprof-css-private.h"
#include "sysprof-progress-cell-private.h"

struct _SysprofProgressCell
{
  GtkWidget  parent_instance;

  AdwBin    *trough;
  AdwBin    *progress;
  GtkLabel  *label;
  GtkLabel  *alt_label;

  double     fraction;
};

enum {
  PROP_0,
  PROP_FRACTION,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofProgressCell, sysprof_progress_cell, GTK_TYPE_WIDGET)

static GParamSpec *properties [N_PROPS];

static void
sysprof_progress_cell_size_allocate (GtkWidget *widget,
                                     int        width,
                                     int        height,
                                     int        baseline)
{
  SysprofProgressCell *self = (SysprofProgressCell *)widget;
  GtkAllocation all = {0, 0, width, height};

  g_assert (SYSPROF_IS_PROGRESS_CELL (self));

  gtk_widget_size_allocate (GTK_WIDGET (self->label), &all, baseline);
  gtk_widget_size_allocate (GTK_WIDGET (self->alt_label), &all, baseline);
  gtk_widget_size_allocate (GTK_WIDGET (self->trough), &all, baseline);

  if (gtk_widget_get_visible (GTK_WIDGET (self->progress)))
    gtk_widget_size_allocate (GTK_WIDGET (self->progress),
                              &(const GtkAllocation) {0, 0, MAX (1, width*self->fraction), height},
                              baseline);
}

static void
sysprof_progress_cell_snapshot (GtkWidget   *widget,
                                GtkSnapshot *snapshot)
{
  SysprofProgressCell *self = (SysprofProgressCell *)widget;
  int w, h;

  g_assert (SYSPROF_IS_PROGRESS_CELL (self));

  gtk_widget_snapshot_child (widget, GTK_WIDGET (self->trough), snapshot);
  gtk_widget_snapshot_child (widget, GTK_WIDGET (self->label), snapshot);
  gtk_widget_snapshot_child (widget, GTK_WIDGET (self->progress), snapshot);

  w = gtk_widget_get_width (widget);
  h = gtk_widget_get_height (widget);

  gtk_snapshot_push_clip (snapshot,
                          &GRAPHENE_RECT_INIT (0, 0, w * self->fraction, h));
  gtk_widget_snapshot_child (widget, GTK_WIDGET (self->alt_label), snapshot);
  gtk_snapshot_pop (snapshot);
}

static void
sysprof_progress_cell_measure (GtkWidget      *widget,
                               GtkOrientation  orientation,
                               int             for_size,
                               int            *minimum,
                               int            *natural,
                               int            *minimum_baseline,
                               int            *natural_baseline)
{
  SysprofProgressCell *self = SYSPROF_PROGRESS_CELL (widget);
  GtkWidget *widgets[] = {
    GTK_WIDGET (self->trough),
    GTK_WIDGET (self->progress),
    GTK_WIDGET (self->label),
    GTK_WIDGET (self->alt_label),
  };

  *minimum = 0;
  *natural = 0;
  *minimum_baseline = -1;
  *natural_baseline = -1;

  for (guint i = 0; i < G_N_ELEMENTS (widgets); i++)
    {
      int child_min, child_nat;

      gtk_widget_measure (widgets[i], orientation, for_size, &child_min, &child_nat, NULL, NULL);

      if (child_min > *minimum)
        *minimum = child_min;

      if (child_nat > *natural)
        *natural = child_nat;
    }
}

static void
sysprof_progress_cell_dispose (GObject *object)
{
  SysprofProgressCell *self = (SysprofProgressCell *)object;

  gtk_widget_unparent (GTK_WIDGET (self->trough));
  gtk_widget_unparent (GTK_WIDGET (self->progress));
  gtk_widget_unparent (GTK_WIDGET (self->label));
  gtk_widget_unparent (GTK_WIDGET (self->alt_label));

  self->trough = NULL;
  self->progress = NULL;
  self->label = NULL;
  self->alt_label = NULL;

  G_OBJECT_CLASS (sysprof_progress_cell_parent_class)->dispose (object);
}

static void
sysprof_progress_cell_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  SysprofProgressCell *self = SYSPROF_PROGRESS_CELL (object);

  switch (prop_id)
    {
    case PROP_FRACTION:
      g_value_set_double (value, sysprof_progress_cell_get_fraction (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_progress_cell_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  SysprofProgressCell *self = SYSPROF_PROGRESS_CELL (object);

  switch (prop_id)
    {
    case PROP_FRACTION:
      sysprof_progress_cell_set_fraction (self, g_value_get_double (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_progress_cell_class_init (SysprofProgressCellClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_progress_cell_dispose;
  object_class->get_property = sysprof_progress_cell_get_property;
  object_class->set_property = sysprof_progress_cell_set_property;

  widget_class->size_allocate = sysprof_progress_cell_size_allocate;
  widget_class->snapshot = sysprof_progress_cell_snapshot;
  widget_class->measure = sysprof_progress_cell_measure;

  properties [PROP_FRACTION] =
    g_param_spec_double ("fraction", NULL, NULL,
                         0, 1, 0,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_css_name (widget_class, "progresscell");
  gtk_widget_class_set_accessible_role (widget_class, GTK_ACCESSIBLE_ROLE_PROGRESS_BAR);
}

static void
sysprof_progress_cell_init (SysprofProgressCell *self)
{
  char percent[32];

  _sysprof_css_init ();

  g_snprintf (percent, sizeof percent, "%6.2lf%%", .0);

  self->label = g_object_new (GTK_TYPE_LABEL,
                              "xalign", 1.f,
                              "valign", GTK_ALIGN_CENTER,
                              "label", percent,
                              "width-chars", 7,
                              NULL);
  self->alt_label = g_object_new (GTK_TYPE_LABEL,
                                  "xalign", 1.f,
                                  "valign", GTK_ALIGN_CENTER,
                                  "label", percent,
                                  "width-chars", 7,
                                  NULL);
  gtk_widget_add_css_class (GTK_WIDGET (self->alt_label), "in-progress");
  self->trough = g_object_new (ADW_TYPE_BIN,
                               "css-name", "trough",
                               NULL);
  self->progress = g_object_new (ADW_TYPE_BIN,
                                 "css-name", "progress",
                                 "visible", FALSE,
                                 NULL);

  gtk_widget_set_parent (GTK_WIDGET (self->trough), GTK_WIDGET (self));
  gtk_widget_set_parent (GTK_WIDGET (self->label), GTK_WIDGET (self));
  gtk_widget_set_parent (GTK_WIDGET (self->progress), GTK_WIDGET (self));
  gtk_widget_set_parent (GTK_WIDGET (self->alt_label), GTK_WIDGET (self));

  gtk_widget_set_overflow (GTK_WIDGET (self), GTK_OVERFLOW_HIDDEN);

  gtk_accessible_update_property (GTK_ACCESSIBLE (self),
                                  GTK_ACCESSIBLE_PROPERTY_VALUE_MAX, 1.0,
                                  GTK_ACCESSIBLE_PROPERTY_VALUE_MIN, 0.0,
                                  GTK_ACCESSIBLE_PROPERTY_VALUE_NOW, 0.0,
                                  -1);
}

GtkWidget *
sysprof_progress_cell_new (void)
{
  return g_object_new (SYSPROF_TYPE_PROGRESS_CELL, NULL);
}

double
sysprof_progress_cell_get_fraction (SysprofProgressCell *self)
{
  g_return_val_if_fail (SYSPROF_IS_PROGRESS_CELL (self), .0);

  return self->fraction;
}

void
sysprof_progress_cell_set_fraction (SysprofProgressCell *self,
                                    double               fraction)
{
  g_return_if_fail (SYSPROF_IS_PROGRESS_CELL (self));

  fraction = CLAMP (fraction, 0., 1.);

  if (fraction != self->fraction)
    {
      char text[32];

      self->fraction = fraction;

      g_snprintf (text, sizeof text, "%6.2lf%%", fraction*100.);
      gtk_label_set_text (self->label, text);
      gtk_label_set_text (self->alt_label, text);

      gtk_widget_set_visible (GTK_WIDGET (self->progress), fraction > .0);

      gtk_accessible_update_property (GTK_ACCESSIBLE (self),
                                      GTK_ACCESSIBLE_PROPERTY_VALUE_MAX, 1.0,
                                      GTK_ACCESSIBLE_PROPERTY_VALUE_MIN, 0.0,
                                      GTK_ACCESSIBLE_PROPERTY_VALUE_NOW, fraction,
                                      GTK_ACCESSIBLE_PROPERTY_VALUE_TEXT, text,
                                      -1);

      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FRACTION]);
      gtk_widget_queue_allocate (GTK_WIDGET (self));
    }
}
