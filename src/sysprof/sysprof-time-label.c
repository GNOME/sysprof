/* sysprof-time-label.c
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

#include <sysprof-capture.h>

#include "sysprof-time-label.h"

struct _SysprofTimeLabel
{
  GtkWidget  parent_instance;
  GtkLabel  *label;
  gint64     t;
  guint      mode : 1;
  guint      show_zero : 1;
};

enum {
  PROP_0,
  PROP_DURATION,
  PROP_TIME_OFFSET,
  PROP_SHOW_ZERO,
  N_PROPS
};

enum {
  MODE_TIME_OFFSET,
  MODE_DURATION,
};

G_DEFINE_FINAL_TYPE (SysprofTimeLabel, sysprof_time_label, GTK_TYPE_WIDGET)

static GParamSpec *properties [N_PROPS];

static void
sysprof_time_label_dispose (GObject *object)
{
  SysprofTimeLabel *self = (SysprofTimeLabel *)object;

  if (self->label)
    {
      gtk_widget_unparent (GTK_WIDGET (self->label));
      self->label = NULL;
    }

  G_OBJECT_CLASS (sysprof_time_label_parent_class)->dispose (object);
}

static void
sysprof_time_label_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  SysprofTimeLabel *self = SYSPROF_TIME_LABEL (object);

  switch (prop_id)
    {
    case PROP_DURATION:
      g_value_set_int64 (value, sysprof_time_label_get_duration (self));
      break;

    case PROP_SHOW_ZERO:
      g_value_set_boolean (value, sysprof_time_label_get_show_zero (self));
      break;

    case PROP_TIME_OFFSET:
      g_value_set_int64 (value, sysprof_time_label_get_time_offset (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_time_label_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  SysprofTimeLabel *self = SYSPROF_TIME_LABEL (object);

  switch (prop_id)
    {
    case PROP_DURATION:
      sysprof_time_label_set_duration (self, g_value_get_int64 (value));
      break;

    case PROP_SHOW_ZERO:
      sysprof_time_label_set_show_zero (self, g_value_get_boolean (value));
      break;

    case PROP_TIME_OFFSET:
      sysprof_time_label_set_time_offset (self, g_value_get_int64 (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_time_label_class_init (SysprofTimeLabelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_time_label_dispose;
  object_class->get_property = sysprof_time_label_get_property;
  object_class->set_property = sysprof_time_label_set_property;

  properties [PROP_DURATION] =
    g_param_spec_int64 ("duration", NULL, NULL,
                        G_MININT64, G_MAXINT64, 0,
                        (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_SHOW_ZERO] =
    g_param_spec_boolean ("show-zero", NULL, NULL,
                          TRUE,
                          (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_TIME_OFFSET] =
    g_param_spec_int64 ("time-offset", NULL, NULL,
                        G_MININT64, G_MAXINT64, 0,
                        (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_set_css_name (widget_class, "timelabel");
}

static void
sysprof_time_label_init (SysprofTimeLabel *self)
{
  static const char *css_classes[] = {"numeric", NULL};
  char str[32];

  self->show_zero = TRUE;

  g_snprintf (str, sizeof str, "%.3lfs", .0);

  self->label = g_object_new (GTK_TYPE_LABEL,
                              "css-classes", css_classes,
                              "xalign", 1.f,
                              "single-line-mode", TRUE,
                              "label", str,
                              NULL);
  gtk_widget_set_parent (GTK_WIDGET (self->label), GTK_WIDGET (self));
}

static void
sysprof_time_label_set_internal (SysprofTimeLabel *self,
                                 gint64            t,
                                 guint             mode)
{
  g_return_if_fail (SYSPROF_IS_TIME_LABEL (self));

  if (mode == self->mode && t == self->t)
    return;

  self->mode = mode;
  self->t = t;

  if (mode == MODE_DURATION)
    {
      char str[32];

      if (t == 0 && !self->show_zero)
        str[0] = 0;
      else if (t == 0)
        g_snprintf (str, sizeof str, "%.3lf s", .0);
      else if (t < 1000000)
        g_snprintf (str, sizeof str, "%.3lf μs", t/1000.);
      else if (t < SYSPROF_NSEC_PER_SEC)
        g_snprintf (str, sizeof str, "%.3lf ms", t/1000000.);
      else
        g_snprintf (str, sizeof str, "%.3lf s", t/(double)SYSPROF_NSEC_PER_SEC);

      gtk_label_set_label (self->label, str);
    }
  else
    {
      char str[32];
      g_snprintf (str, sizeof str, "%0.3lf s", t/(double)SYSPROF_NSEC_PER_SEC);
      gtk_label_set_label (self->label, str);
    }

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_DURATION]);
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_TIME_OFFSET]);
}

gint64
sysprof_time_label_get_duration (SysprofTimeLabel *self)
{
  if (self->mode == MODE_DURATION)
    return self->t;
  return 0;
}

void
sysprof_time_label_set_duration (SysprofTimeLabel *self,
                                 gint64            duration)
{
  sysprof_time_label_set_internal (self, duration, MODE_DURATION);
}

gint64
sysprof_time_label_get_time_offset (SysprofTimeLabel *self)
{
  if (self->mode == MODE_TIME_OFFSET)
    return self->t;
  return 0;
}

void
sysprof_time_label_set_time_offset (SysprofTimeLabel *self,
                                    gint64            time_offset)
{
  sysprof_time_label_set_internal (self, time_offset, MODE_TIME_OFFSET);
}

gboolean
sysprof_time_label_get_show_zero (SysprofTimeLabel *self)
{
  g_return_val_if_fail (SYSPROF_IS_TIME_LABEL (self), FALSE);

  return self->show_zero;
}

void
sysprof_time_label_set_show_zero (SysprofTimeLabel *self,
                                  gboolean          show_zero)
{
  g_return_if_fail (SYSPROF_IS_TIME_LABEL (self));

  show_zero = !!show_zero;

  if (self->show_zero != show_zero)
    {
      self->show_zero = show_zero;

      if (self->t == 0)
        {
          char str[32];

          g_snprintf (str, sizeof str, "%0.3lfs", .0);

          if (show_zero)
            gtk_label_set_label (self->label, str);
          else
            gtk_label_set_label (self->label, NULL);
        }

      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_ZERO]);
    }
}
