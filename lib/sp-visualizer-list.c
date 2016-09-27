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

#define NSEC_PER_SEC G_GUINT64_CONSTANT(1000000000)

typedef struct
{
  SpCaptureReader *reader;
  gint64 begin_time;
  gint64 end_time;
} SpVisualizerListPrivate;

enum {
  PROP_0,
  PROP_READER,
  N_PROPS
};

G_DEFINE_TYPE_WITH_PRIVATE (SpVisualizerList, sp_visualizer_list, GTK_TYPE_LIST_BOX)

static GParamSpec *properties [N_PROPS];

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
  SpVisualizerList *self = (SpVisualizerList *)object;

  switch (prop_id)
    {
    case PROP_READER:
      g_value_set_boxed (value, sp_visualizer_list_get_reader (self));
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
  SpVisualizerList *self = (SpVisualizerList *)object;

  switch (prop_id)
    {
    case PROP_READER:
      sp_visualizer_list_set_reader (self, g_value_get_boxed (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_visualizer_list_class_init (SpVisualizerListClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sp_visualizer_list_finalize;
  object_class->get_property = sp_visualizer_list_get_property;
  object_class->set_property = sp_visualizer_list_set_property;

  properties [PROP_READER] =
    g_param_spec_boxed ("reader",
                        "Reader",
                        "The capture reader",
                        SP_TYPE_CAPTURE_READER,
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

static void
propagate_time (GtkWidget *widget,
                gpointer   user_data)
{
  struct {
    gint64 begin;
    gint64 end;
  } *t = user_data;

  if (SP_IS_VISUALIZER_ROW (widget))
    sp_visualizer_row_set_time_range (SP_VISUALIZER_ROW (widget), t->begin, t->end);
}

void
sp_visualizer_list_set_time_range (SpVisualizerList *self,
                                   gint64            begin_time,
                                   gint64            end_time)
{
  struct {
    gint64 begin;
    gint64 end;
  } t = { begin_time, end_time };

  g_return_if_fail (SP_IS_VISUALIZER_LIST (self));

  gtk_container_foreach (GTK_CONTAINER (self), propagate_time, &t);
}

void
sp_visualizer_list_get_time_range (SpVisualizerList *self,
                                   gint64           *begin_time,
                                   gint64           *end_time)
{
  SpVisualizerListPrivate *priv = sp_visualizer_list_get_instance_private (self);

  g_return_if_fail (SP_IS_VISUALIZER_LIST (self));

  if (begin_time)
    *begin_time = priv->begin_time;

  if (end_time)
    *end_time = priv->end_time;
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

static void
propagate_reader (GtkWidget *widget,
                  gpointer   user_data)
{
  SpCaptureReader *reader = user_data;

  if (SP_IS_VISUALIZER_ROW (widget))
    sp_visualizer_row_set_reader (SP_VISUALIZER_ROW (widget), reader);
}

static void
sp_visualizer_list_update_time_range (SpVisualizerList *self)
{
  SpVisualizerListPrivate *priv = sp_visualizer_list_get_instance_private (self);
  gint64 begin_time = 0;
  gint64 end_time = 0;

  g_return_if_fail (SP_IS_VISUALIZER_LIST (self));

  if (priv->reader != NULL)
    {
      begin_time = sp_capture_reader_get_start_time (priv->reader);
      end_time = begin_time + (NSEC_PER_SEC * 60);
    }

  sp_visualizer_list_set_time_range (self, begin_time, end_time);
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
      if (reader)
        priv->reader = sp_capture_reader_ref (reader);
      gtk_container_foreach (GTK_CONTAINER (self), propagate_reader, reader);
      sp_visualizer_list_update_time_range (self);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_READER]);
    }
}
