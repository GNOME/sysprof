/* sysprof-dbus-section.c
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

#include "sysprof-chart.h"
#include "sysprof-dbus-section.h"
#include "sysprof-dbus-utility.h"
#include "sysprof-line-layer.h"
#include "sysprof-time-filter-model.h"
#include "sysprof-time-scrubber.h"
#include "sysprof-time-series.h"
#include "sysprof-time-span-layer.h"
#include "sysprof-value-axis.h"
#include "sysprof-xy-series.h"

struct _SysprofDBusSection
{
  SysprofSection       parent_instance;

  SysprofTimeScrubber *scrubber;
  GtkColumnView       *column_view;
  GtkColumnViewColumn *time_column;
};

G_DEFINE_FINAL_TYPE (SysprofDBusSection, sysprof_dbus_section, SYSPROF_TYPE_SECTION)

static char *
format_size (gpointer data,
             guint    size)
{
  if (size == 0)
    return NULL;
  return g_format_size (size);
}

static char *
format_serial (gpointer data,
               guint    serial)
{
  if (serial == 0 || serial == (guint)-1)
    return NULL;
  return g_strdup_printf ("0x%x", serial);
}

static char *
format_message_type (gpointer         data,
                     GDBusMessageType type)
{
  static GEnumClass *klass;
  GEnumValue *value;

  if (klass == NULL)
    klass = g_type_class_ref (G_TYPE_DBUS_MESSAGE_TYPE);

  if ((value = g_enum_get_value (klass, type)))
    return g_strdup (value->value_nick);

  return NULL;
}

char *
flags_to_string (GType type,
                 guint value)
{
  GFlagsClass *flags_class;
  GFlagsValue *val;
  GString *str;

  g_return_val_if_fail (G_TYPE_IS_FLAGS (type), NULL);

  if (value == 0)
    return NULL;

  if (!(flags_class = g_type_class_ref (type)))
    return NULL;

  str = g_string_new (NULL);

  while ((str->len == 0 || value != 0) &&
         (val = g_flags_get_first_value (flags_class, value)))
    {
      if (str->len > 0)
        g_string_append_c (str, '|');
      g_string_append (str, val->value_nick);
      value &= ~val->value;
    }

  if (value)
    {
      if (str->len > 0)
        g_string_append_c (str, '|');
      g_string_append_printf (str, "0x%x", value);
    }

  return g_string_free (str, FALSE);
}

static char *
format_flags (gpointer data,
              guint    flags)
{
  if (flags == 0)
    return NULL;

  return flags_to_string (G_TYPE_DBUS_MESSAGE_FLAGS, flags);
}

static char *
format_bus_type (gpointer data,
                 guint    bus_type)
{
  if (bus_type == G_BUS_TYPE_SESSION)
    return g_strdup ("Session");
  else if (bus_type == G_BUS_TYPE_SYSTEM)
    return g_strdup ("System");

  return NULL;
}

static guint
cast_bus_type (gpointer data,
               guint    bus_type)
{
  return bus_type;
}

static char *
format_number (gpointer unused,
               guint    number)
{
  if (number == 0)
    return NULL;
  return g_strdup_printf ("%'u", number);
}

static void
sysprof_dbus_section_dispose (GObject *object)
{
  SysprofDBusSection *self = (SysprofDBusSection *)object;

  gtk_widget_dispose_template (GTK_WIDGET (self), SYSPROF_TYPE_DBUS_SECTION);

  G_OBJECT_CLASS (sysprof_dbus_section_parent_class)->dispose (object);
}

static void
sysprof_dbus_section_class_init (SysprofDBusSectionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_dbus_section_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-dbus-section.ui");
  gtk_widget_class_bind_template_child (widget_class, SysprofDBusSection, column_view);
  gtk_widget_class_bind_template_child (widget_class, SysprofDBusSection, scrubber);
  gtk_widget_class_bind_template_child (widget_class, SysprofDBusSection, time_column);
  gtk_widget_class_bind_template_callback (widget_class, cast_bus_type);
  gtk_widget_class_bind_template_callback (widget_class, format_bus_type);
  gtk_widget_class_bind_template_callback (widget_class, format_flags);
  gtk_widget_class_bind_template_callback (widget_class, format_serial);
  gtk_widget_class_bind_template_callback (widget_class, format_size);
  gtk_widget_class_bind_template_callback (widget_class, format_number);
  gtk_widget_class_bind_template_callback (widget_class, format_message_type);

  g_type_ensure (SYSPROF_TYPE_CHART);
  g_type_ensure (SYSPROF_TYPE_DBUS_UTILITY);
  g_type_ensure (SYSPROF_TYPE_DOCUMENT_MARK);
  g_type_ensure (SYSPROF_TYPE_DOCUMENT_DBUS_MESSAGE);
}

static void
sysprof_dbus_section_init (SysprofDBusSection *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_column_view_sort_by_column (self->column_view, self->time_column, GTK_SORT_ASCENDING);
}
