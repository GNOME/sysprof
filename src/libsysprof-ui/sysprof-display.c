/* sysprof-display.c
 *
 * Copyright 2019 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sysprof-display"

#include "config.h"

#include <glib/gi18n.h>

#include <sysprof-capture.h>
#include <sysprof-ui.h>
#include <sysprof.h>

#include "sysprof-capture-view.h"
#include "sysprof-display.h"
#include "sysprof-empty-state-view.h"
#include "sysprof-recording-state-view.h"

typedef struct
{
  SysprofCaptureView        *capture_view;
  SysprofEmptyStateView     *empty_view;
  SysprofRecordingStateView *recording_view;
} SysprofDisplayPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (SysprofDisplay, sysprof_display, GTK_TYPE_BIN)

enum {
  PROP_0,
  PROP_TITLE,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

/**
 * sysprof_display_new:
 *
 * Create a new #SysprofDisplay.
 *
 * Returns: (transfer full): a newly created #SysprofDisplay
 *
 * Since: 3.34
 */
GtkWidget *
sysprof_display_new (void)
{
  return g_object_new (SYSPROF_TYPE_DISPLAY, NULL);
}

static gchar *
sysprof_display_dup_title (SysprofDisplay *self)
{
  SysprofDisplayPrivate *priv = sysprof_display_get_instance_private (self);
  SysprofCaptureReader *reader;

  g_return_val_if_fail (SYSPROF_IS_DISPLAY (self), NULL);

  if ((reader = sysprof_capture_view_get_reader (priv->capture_view)))
    {
      const gchar *filename;

      if ((filename = sysprof_capture_reader_get_filename (reader)))
        return g_strdup (filename);

    }

  return g_strdup (_("Unsaved Session"));
}

static void
sysprof_display_finalize (GObject *object)
{
  G_OBJECT_CLASS (sysprof_display_parent_class)->finalize (object);
}

static void
sysprof_display_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  SysprofDisplay *self = (SysprofDisplay *)object;

  switch (prop_id)
    {
    case PROP_TITLE:
      g_value_take_string (value, sysprof_display_dup_title (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_display_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_display_class_init (SysprofDisplayClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = sysprof_display_finalize;
  object_class->get_property = sysprof_display_get_property;
  object_class->set_property = sysprof_display_set_property;

  properties [PROP_TITLE] =
    g_param_spec_string ("title",
                         "Title",
                         "The title of the display",
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/ui/sysprof-display.ui");
  gtk_widget_class_bind_template_child_private (widget_class, SysprofDisplay , empty_view);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofDisplay , recording_view);
  gtk_widget_class_bind_template_child_private (widget_class, SysprofDisplay , capture_view);

  g_type_ensure (SYSPROF_TYPE_CAPTURE_VIEW);
  g_type_ensure (SYSPROF_TYPE_EMPTY_STATE_VIEW);
  g_type_ensure (SYSPROF_TYPE_RECORDING_STATE_VIEW);
}

static void
sysprof_display_init (SysprofDisplay *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}
