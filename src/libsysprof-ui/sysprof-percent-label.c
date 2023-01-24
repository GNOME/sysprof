/* sysprof-percent-label.c
 *
 * Copyright 2023 Corentin Noël <corentin.noel@collabora.com>
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

#include <glib/gi18n.h>

#include "sysprof-percent-label.h"

struct _SysprofPercentLabel
{
  GtkWidget parent_instance;

  GtkProgressBar *progress_bar;

  double percent;
};

G_DEFINE_FINAL_TYPE (SysprofPercentLabel, sysprof_percent_label, GTK_TYPE_WIDGET)

enum {
  PROP_0,
  PROP_PERCENT,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

SysprofPercentLabel *
sysprof_percent_label_new (void)
{
  return g_object_new (SYSPROF_TYPE_PERCENT_LABEL, NULL);
}

gchar *
percent_label (GObject *object,
               gdouble  percent)
{
  return g_strdup_printf (C_("progress bar label", "%d %%"), (int)percent);
}

gdouble
percent_to_fraction (GObject *object,
                     gdouble  percent)
{
  return percent/100.0;
}

static void
sysprof_percent_label_finalize (GObject *object)
{
  GtkWidget *child;

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (object))))
    gtk_widget_unparent (child);

  G_OBJECT_CLASS (sysprof_percent_label_parent_class)->finalize (object);
}

static void
sysprof_percent_label_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  SysprofPercentLabel *self = SYSPROF_PERCENT_LABEL (object);

  switch (prop_id)
    {
    case PROP_PERCENT:
      g_value_set_double (value, sysprof_percent_label_get_percent (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_percent_label_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  SysprofPercentLabel *self = SYSPROF_PERCENT_LABEL (object);

  switch (prop_id)
    {
    case PROP_PERCENT:
      sysprof_percent_label_set_percent (self, g_value_get_double (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_percent_label_measure (GtkWidget      *widget,
                               GtkOrientation  orientation,
                               int             for_size,
                               int            *minimum,
                               int            *natural,
                               int            *minimum_baseline,
                               int            *natural_baseline)
{
  SysprofPercentLabel *self = (SysprofPercentLabel *)widget;

  gtk_widget_measure (GTK_WIDGET (self->progress_bar),
                      orientation,
                      for_size,
                      minimum,
                      natural,
                      minimum_baseline,
                      natural_baseline);
  if (orientation == GTK_ORIENTATION_HORIZONTAL) {
    PangoRectangle logical_rect;
    PangoLayout *layout;
    char *text;
    int allocation;

    text = g_strdup_printf (C_("progress bar label", "%d %%"), 100);
    layout = gtk_widget_create_pango_layout (GTK_WIDGET (self->progress_bar), text);
    g_free (text);

    pango_layout_get_pixel_extents (layout, NULL, &logical_rect);
    allocation = logical_rect.width +
                 gtk_widget_get_margin_start (GTK_WIDGET (self->progress_bar)) +
                 gtk_widget_get_margin_end (GTK_WIDGET (self->progress_bar));
    if (minimum)
      *minimum = allocation;

    if (natural)
      *natural = allocation;

    g_object_unref (layout);
  }
}

static void
sysprof_percent_label_snapshot (GtkWidget   *widget,
                                GtkSnapshot *snapshot)
{
  SysprofPercentLabel *self = (SysprofPercentLabel *)widget;
  gtk_widget_snapshot_child (widget, GTK_WIDGET (self->progress_bar), snapshot);
}

static void
sysprof_percent_label_size_allocate (GtkWidget *widget,
                                     int        width,
                                     int        height,
                                     int        baseline)
{
  SysprofPercentLabel *self = (SysprofPercentLabel *)widget;
  GtkAllocation alloc = {
    0, 0,
    width, height
  };
  gtk_widget_size_allocate (GTK_WIDGET (self->progress_bar),
                            &alloc,
                            baseline);
}

static void
sysprof_percent_label_class_init (SysprofPercentLabelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = sysprof_percent_label_finalize;
  object_class->get_property = sysprof_percent_label_get_property;
  object_class->set_property = sysprof_percent_label_set_property;

  widget_class->measure = sysprof_percent_label_measure;
  widget_class->snapshot = sysprof_percent_label_snapshot;
  widget_class->size_allocate = sysprof_percent_label_size_allocate;

  properties [PROP_PERCENT] =
    g_param_spec_double ("percent",
                         "Percent",
                         "The percent",
                         0.0, 100., 0.0,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/sysprof/ui/sysprof-percent-label.ui");
  gtk_widget_class_bind_template_child (widget_class, SysprofPercentLabel, progress_bar);
  gtk_widget_class_bind_template_callback (widget_class, percent_label);
  gtk_widget_class_bind_template_callback (widget_class, percent_to_fraction);
}

static void
sysprof_percent_label_init (SysprofPercentLabel *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

gdouble
sysprof_percent_label_get_percent (SysprofPercentLabel *self)
{
  g_return_val_if_fail (SYSPROF_IS_PERCENT_LABEL (self), 0.);

  return self->percent;
}

void
sysprof_percent_label_set_percent (SysprofPercentLabel *self,
                                   gdouble              percent)
{
  g_return_if_fail (SYSPROF_IS_PERCENT_LABEL (self));

  self->percent = percent;
}
