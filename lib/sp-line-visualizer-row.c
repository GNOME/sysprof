/* sp-line-visualizer-row.c
 *
 * Copyright (C) 2016 Christian Hergert <christian@hergert.me>
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

#define G_LOG_DOMAIN "sp-line-visualizer-row"

#include <stdlib.h>

#include "sp-line-visualizer-row.h"

typedef struct
{
  gint64                time;
  SpCaptureCounterValue value;
} LineDataItem;

typedef struct
{
  guint   id;
  guint   type;
  GArray *values;
} LineData;

typedef struct
{
  SpCaptureReader *reader;

  GHashTable      *counters;
  GtkLabel        *label;

  gint64           start_time;
  gint64           duration;

  guint            needs_recalc : 1;
} SpLineVisualizerRowPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (SpLineVisualizerRow, sp_line_visualizer_row, SP_TYPE_VISUALIZER_ROW)

enum {
  PROP_0,
  PROP_TITLE,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

static void
line_data_free (gpointer data)
{
  LineData *ld = data;

  if (ld != NULL)
    {
      g_array_unref (ld->values);
      g_free (ld);
    }
}

static void
sp_line_visualizer_row_recalc (SpLineVisualizerRow *self)
{
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);
  GHashTable *new_data;
  SpCaptureFrame frame;

  g_assert (SP_IS_LINE_VISUALIZER_ROW (self));

  /*
   * TODO:
   *
   * We want to create an index that gives us quick access to this
   * data without quite so much overhead that we have now. The 16
   * bytes per data-point is pretty high.
   */

  priv->needs_recalc = FALSE;

  if (priv->reader == NULL)
    return;

  sp_capture_reader_reset (priv->reader);

  priv->start_time = sp_capture_reader_get_start_time (priv->reader);
  priv->duration = 0;

  new_data = g_hash_table_new_full (NULL, NULL, NULL, line_data_free);

  while (sp_capture_reader_peek_frame (priv->reader, &frame))
    {
      gint64 relative;

      if (frame.time < priv->start_time)
        frame.time = priv->start_time;

      relative = frame.time - priv->start_time;
      if (relative > priv->duration)
        priv->duration = relative;

      if (frame.type == SP_CAPTURE_FRAME_CTRDEF)
        {
          const SpCaptureFrameCounterDefine *def;
          guint i;

          if (NULL == (def = sp_capture_reader_read_counter_define (priv->reader)))
            return;

          for (i = 0; i < def->n_counters; i++)
            {
              const SpCaptureCounter *ctr = &def->counters[i];

              if (g_hash_table_contains (priv->counters, GSIZE_TO_POINTER (ctr->id)))
                {
                  LineData *ld = g_hash_table_lookup (new_data, GSIZE_TO_POINTER (ctr->id));
                  LineDataItem item;

                  if (ld == NULL)
                    {
                      ld = g_new (LineData, 1);
                      ld->id = ctr->id;
                      ld->type = ctr->type;
                      ld->values = g_array_new (FALSE, FALSE, sizeof (LineDataItem));
                      g_hash_table_insert (new_data, GSIZE_TO_POINTER (ctr->id), ld);
                    }

                  item.time = MAX (priv->start_time, def->frame.time) - priv->start_time;
                  item.value = ctr->value;

                  g_array_append_val (ld->values, item);
                }
            }
        }
      else if (frame.type == SP_CAPTURE_FRAME_CTRSET)
        {
          const SpCaptureFrameCounterSet *set;
          LineDataItem item;
          guint i;

          if (NULL == (set = sp_capture_reader_read_counter_set (priv->reader)))
            return;

          item.time = MAX (priv->start_time, set->frame.time) - priv->start_time;

          for (i = 0; i < set->n_values; i++)
            {
              const SpCaptureCounterValues *values = &set->values[i];
              guint j;

              for (j = 0; j < G_N_ELEMENTS (values->values); j++)
                {
                  LineData *ld;

                  if (values->ids[j] == 0)
                    break;

                  ld = g_hash_table_lookup (new_data, GSIZE_TO_POINTER (values->ids[j]));

                  /* possible there was no matching ctrdef */
                  if (ld == NULL)
                    continue;

                  item.value = values->values[j];

                  g_array_append_val (ld->values, item);
                }
            }
        }
      else if (!sp_capture_reader_skip (priv->reader))
        return;
    }

  g_clear_pointer (&priv->counters, g_hash_table_unref);
  priv->counters = new_data;
}

static inline gdouble
calc_y (SpLineVisualizerRow   *self,
        LineData              *ld,
        SpCaptureCounterValue  value)
{
  return value.vdbl / 100.0;
}

static gboolean
sp_line_visualizer_row_draw (GtkWidget *widget,
                             cairo_t   *cr)
{
  SpLineVisualizerRow *self = (SpLineVisualizerRow *)widget;
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);
  GtkAllocation alloc;
  GHashTableIter iter;
  gdouble duration;
  gboolean ret;
  gpointer key, val;

  g_assert (SP_IS_LINE_VISUALIZER_ROW (widget));
  g_assert (cr != NULL);

  if (priv->needs_recalc)
    sp_line_visualizer_row_recalc (self);

  gtk_widget_get_allocation (widget, &alloc);

  ret = GTK_WIDGET_CLASS (sp_line_visualizer_row_parent_class)->draw (widget, cr);

  duration = (gdouble)priv->duration;

  g_hash_table_iter_init (&iter, priv->counters);

  while (g_hash_table_iter_next (&iter, &key, &val))
    {
      LineData *ld = val;

      cairo_save (cr);
      cairo_move_to (cr, 0, alloc.height);

      for (guint i = 0; i < ld->values->len; i++)
        {
          LineDataItem *item = &g_array_index (ld->values, LineDataItem, i);
          gdouble x;
          gdouble y;

          x = (item->time / duration) * alloc.width;
          y = alloc.height - ((item->value.vdbl / 100.0) * alloc.height);

          cairo_line_to (cr, x, y);
        }

      cairo_line_to (cr, alloc.width, alloc.height);
      cairo_line_to (cr, 0, alloc.height);

      SP_LINE_VISUALIZER_ROW_GET_CLASS (self)->prepare (self, cr, ld->id);

      cairo_stroke_preserve (cr);
      cairo_fill (cr);
      cairo_restore (cr);
    }

  return ret;
}

static void
sp_line_visualizer_row_set_reader (SpVisualizerRow *row,
                                   SpCaptureReader *reader)
{
  SpLineVisualizerRow *self = (SpLineVisualizerRow *)row;
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);

  g_assert (SP_IS_LINE_VISUALIZER_ROW (self));

  if (priv->reader != reader)
    {
      if (priv->reader != NULL)
        {
          sp_capture_reader_unref (priv->reader);
          priv->reader = NULL;
          priv->start_time = 0;
          priv->duration = 0;
        }

      if (reader != NULL)
        {
          priv->reader = sp_capture_reader_ref (reader);
          priv->start_time = 0;
          priv->duration = 0;
          priv->needs_recalc = TRUE;
        }

      gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}

static void
sp_line_visualizer_row_real_prepare (SpLineVisualizerRow *self,
                                     cairo_t             *cr,
                                     guint                counter_id)
{
  g_assert (SP_IS_LINE_VISUALIZER_ROW (self));
  g_assert (cr != NULL);

  cairo_set_line_width (cr, 1.0);
  cairo_set_source_rgba (cr, 0, 0, 0, 0.4);
}

static void
sp_line_visualizer_row_finalize (GObject *object)
{
  SpLineVisualizerRow *self = (SpLineVisualizerRow *)object;
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);

  g_clear_pointer (&priv->counters, g_hash_table_unref);
  g_clear_pointer (&priv->reader, sp_capture_reader_unref);

  G_OBJECT_CLASS (sp_line_visualizer_row_parent_class)->finalize (object);
}

static void
sp_line_visualizer_row_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  SpLineVisualizerRow *self = SP_LINE_VISUALIZER_ROW (object);
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_TITLE:
      g_object_get_property (G_OBJECT (priv->label), "label", value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_line_visualizer_row_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  SpLineVisualizerRow *self = SP_LINE_VISUALIZER_ROW (object);
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_TITLE:
      g_object_set_property (G_OBJECT (priv->label), "label", value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_line_visualizer_row_class_init (SpLineVisualizerRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  SpVisualizerRowClass *visualizer_class = SP_VISUALIZER_ROW_CLASS (klass);

  klass->prepare = sp_line_visualizer_row_real_prepare;

  object_class->finalize = sp_line_visualizer_row_finalize;
  object_class->get_property = sp_line_visualizer_row_get_property;
  object_class->set_property = sp_line_visualizer_row_set_property;

  widget_class->draw = sp_line_visualizer_row_draw;

  visualizer_class->set_reader = sp_line_visualizer_row_set_reader;

  properties [PROP_TITLE] =
    g_param_spec_string ("title",
                         "Title",
                         "The title of the row",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sp_line_visualizer_row_init (SpLineVisualizerRow *self)
{
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);
  PangoAttrList *attrs = pango_attr_list_new ();

  priv->counters = g_hash_table_new (NULL, NULL);

  pango_attr_list_insert (attrs, pango_attr_scale_new (PANGO_SCALE_SMALL * PANGO_SCALE_SMALL));

  priv->label = g_object_new (GTK_TYPE_LABEL,
                              "attributes", attrs,
                              "visible", TRUE,
                              "xalign", 0.0f,
                              "yalign", 0.0f,
                              NULL);
  gtk_container_add (GTK_CONTAINER (self), GTK_WIDGET (priv->label));

  pango_attr_list_unref (attrs);
}

void
sp_line_visualizer_row_add_counter (SpLineVisualizerRow *self,
                                    guint                counter_id)
{
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);

  g_assert (SP_IS_LINE_VISUALIZER_ROW (self));
  g_assert (priv->counters != NULL);

  g_hash_table_insert (priv->counters, GSIZE_TO_POINTER (counter_id), NULL);

  if (SP_LINE_VISUALIZER_ROW_GET_CLASS (self)->counter_added)
    SP_LINE_VISUALIZER_ROW_GET_CLASS (self)->counter_added (self, counter_id);

  priv->needs_recalc = TRUE;

  gtk_widget_queue_draw (GTK_WIDGET (self));
}

void
sp_line_visualizer_row_clear (SpLineVisualizerRow *self)
{
  SpLineVisualizerRowPrivate *priv = sp_line_visualizer_row_get_instance_private (self);

  g_return_if_fail (SP_IS_LINE_VISUALIZER_ROW (self));

  g_hash_table_remove_all (priv->counters);
  gtk_widget_queue_resize (GTK_WIDGET (self));
}
