/* sp-visualizer-list.c
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

#define G_LOG_DOMAIN "sp-visualizer-list"

#include <glib/gi18n.h>

#include "sp-visualizer-list.h"
#include "sp-visualizer-row.h"
#include "sp-zoom-manager.h"

#define NSEC_PER_SEC              G_GUINT64_CONSTANT(1000000000)
#define DEFAULT_PIXELS_PER_SECOND 20

typedef struct
{
  SpCaptureReader *reader;
  SpZoomManager *zoom_manager;
  gint64 begin_time;
  gint64 end_time;
} SpVisualizerListPrivate;

enum {
  PROP_0,
  PROP_READER,
  PROP_ZOOM_MANAGER,
  N_PROPS
};

G_DEFINE_TYPE_WITH_PRIVATE (SpVisualizerList, sp_visualizer_list, GTK_TYPE_LIST_BOX)

static GParamSpec *properties [N_PROPS];

static void
sp_visualizer_list_add (GtkContainer *container,
                        GtkWidget    *widget)
{
  SpVisualizerList *self = (SpVisualizerList *)container;
  SpVisualizerListPrivate *priv = sp_visualizer_list_get_instance_private (self);

  GTK_CONTAINER_CLASS (sp_visualizer_list_parent_class)->add (container, widget);

  if (SP_IS_VISUALIZER_ROW (widget))
    {
      sp_visualizer_row_set_reader (SP_VISUALIZER_ROW (widget), priv->reader);
      sp_visualizer_row_set_zoom_manager (SP_VISUALIZER_ROW (widget), priv->zoom_manager);
    }
}

static void
sp_visualizer_list_finalize (GObject *object)
{
  SpVisualizerList *self = (SpVisualizerList *)object;
  SpVisualizerListPrivate *priv = sp_visualizer_list_get_instance_private (self);

  g_clear_pointer (&priv->reader, sp_capture_reader_unref);

  G_OBJECT_CLASS (sp_visualizer_list_parent_class)->finalize (object);
}

static void
sp_visualizer_list_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  SpVisualizerList *self = SP_VISUALIZER_LIST (object);

  switch (prop_id)
    {
    case PROP_READER:
      g_value_set_boxed (value, sp_visualizer_list_get_reader (self));
      break;

    case PROP_ZOOM_MANAGER:
      g_value_set_object (value, sp_visualizer_list_get_zoom_manager (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_visualizer_list_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  SpVisualizerList *self = SP_VISUALIZER_LIST (object);

  switch (prop_id)
    {
    case PROP_READER:
      sp_visualizer_list_set_reader (self, g_value_get_boxed (value));
      break;

    case PROP_ZOOM_MANAGER:
      sp_visualizer_list_set_zoom_manager (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_visualizer_list_class_init (SpVisualizerListClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkContainerClass *container_class = GTK_CONTAINER_CLASS (klass);

  object_class->finalize = sp_visualizer_list_finalize;
  object_class->get_property = sp_visualizer_list_get_property;
  object_class->set_property = sp_visualizer_list_set_property;

  container_class->add = sp_visualizer_list_add;

  properties [PROP_READER] =
    g_param_spec_boxed ("reader",
                        "Reader",
                        "The capture reader",
                        SP_TYPE_CAPTURE_READER,
                        (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_ZOOM_MANAGER] =
    g_param_spec_object ("zoom-manager",
                         "Zoom Manager",
                         "The zoom manager",
                         SP_TYPE_ZOOM_MANAGER,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sp_visualizer_list_init (SpVisualizerList *self)
{
}

GtkWidget *
sp_visualizer_list_new (void)
{
  return g_object_new (SP_TYPE_VISUALIZER_ROW, NULL);
}

/**
 * sp_visualizer_list_get_reader:
 *
 * Gets the reader that is being used for the #SpVisualizerList.
 *
 * Returns: (transfer none): An #SpCaptureReader
 */
SpCaptureReader *
sp_visualizer_list_get_reader (SpVisualizerList *self)
{
  SpVisualizerListPrivate *priv = sp_visualizer_list_get_instance_private (self);

  g_return_val_if_fail (SP_IS_VISUALIZER_LIST (self), NULL);

  return priv->reader;
}

void
sp_visualizer_list_set_reader (SpVisualizerList *self,
                               SpCaptureReader  *reader)
{
  SpVisualizerListPrivate *priv = sp_visualizer_list_get_instance_private (self);

  g_return_if_fail (SP_IS_VISUALIZER_LIST (self));

  if (reader != priv->reader)
    {
      g_clear_pointer (&priv->reader, sp_capture_reader_unref);

      if (reader != NULL)
        priv->reader = sp_capture_reader_ref (reader);

      gtk_container_foreach (GTK_CONTAINER (self),
                             (GtkCallback)sp_visualizer_row_set_reader,
                             reader);

      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_READER]);
    }
}

void
sp_visualizer_list_set_zoom_manager (SpVisualizerList *self,
                                     SpZoomManager    *zoom_manager)
{
  SpVisualizerListPrivate *priv = sp_visualizer_list_get_instance_private (self);

  g_return_if_fail (SP_IS_VISUALIZER_LIST (self));
  g_return_if_fail (SP_IS_ZOOM_MANAGER (zoom_manager));

  if (g_set_object (&priv->zoom_manager, zoom_manager))
    {
      gtk_container_foreach (GTK_CONTAINER (self),
                             (GtkCallback)sp_visualizer_row_set_zoom_manager,
                             zoom_manager);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_ZOOM_MANAGER]);
    }
}

/**
 * sp_visualizer_list_get_zoom_manager:
 *
 * Returns: (nullable) (transfer): A #SpZoomManager or %NULL.
 */
SpZoomManager *
sp_visualizer_list_get_zoom_manager (SpVisualizerList *self)
{
  SpVisualizerListPrivate *priv = sp_visualizer_list_get_instance_private (self);

  g_return_val_if_fail (SP_IS_VISUALIZER_LIST (self), NULL);

  return priv->zoom_manager;
}
