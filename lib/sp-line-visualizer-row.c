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

struct _SpLineVisualizerRow
{
  SpVisualizerRow  parent_instance;

  SpCaptureReader *reader;

  GHashTable      *counters;

  GtkLabel        *label;

  guint            needs_recalc : 1;

  gint64           start_time;
  gint64           duration;
};

G_DEFINE_TYPE (SpLineVisualizerRow, sp_line_visualizer_row, SP_TYPE_VISUALIZER_ROW)

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
  GHashTable *new_data;
  SpCaptureFrame frame;

  g_assert (SP_IS_LINE_VISUALIZER_ROW (self));

  self->needs_recalc = FALSE;

  if (self->reader == NULL)
    return;

  sp_capture_reader_reset (self->reader);

  self->start_time = sp_capture_reader_get_start_time (self->reader);
  self->duration = 0;

  new_data = g_hash_table_new_full (NULL, NULL, NULL, line_data_free);

  while (sp_capture_reader_peek_frame (self->reader, &frame))
    {
      gint64 relative;

      if (frame.time < self->start_time)
        frame.time = self->start_time;

      relative = frame.time - self->start_time;
      if (relative > self->duration)
        self->duration = relative;

      if (frame.type == SP_CAPTURE_FRAME_CTRDEF)
        {
          const SpCaptureFrameCounterDefine *def;
          guint i;

          if (NULL == (def = sp_capture_reader_read_counter_define (self->reader)))
            return;

          for (i = 0; i < def->n_counters; i++)
            {
              const SpCaptureCounter *ctr = &def->counters[i];

              if (g_hash_table_contains (self->counters, GSIZE_TO_POINTER (ctr->id)))
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

                  item.time = MAX (self->start_time, def->frame.time) - self->start_time;
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

          if (NULL == (set = sp_capture_reader_read_counter_set (self->reader)))
            return;

          item.time = MAX (self->start_time, set->frame.time) - self->start_time;

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
      else if (!sp_capture_reader_skip (self->reader))
        return;
    }

  g_clear_pointer (&self->counters, g_hash_table_unref);
  self->counters = new_data;
}

static inline gdouble
calc_x (SpLineVisualizerRow *self,
        gint64               time)
{
  gdouble ret = (gdouble)time / (gdouble)self->duration;
  return ret;
}

static inline gdouble
calc_y (SpLineVisualizerRow   *self,
        LineData              *ld,
        SpCaptureCounterValue  value)
{
  gdouble ret = value.vdbl / 100.0;
  return ret;
}

static gboolean
sp_line_visualizer_row_draw (GtkWidget *widget,
                             cairo_t   *cr)
{
  SpLineVisualizerRow *self = (SpLineVisualizerRow *)widget;
  GtkAllocation alloc;
  GHashTableIter iter;
  gboolean ret;
  gpointer key, val;
  guint i;

  g_assert (SP_IS_LINE_VISUALIZER_ROW (widget));
  g_assert (cr != NULL);

  if (self->needs_recalc)
    sp_line_visualizer_row_recalc (self);

  gtk_widget_get_allocation (widget, &alloc);

  ret = GTK_WIDGET_CLASS (sp_line_visualizer_row_parent_class)->draw (widget, cr);

  cairo_save (cr);

  g_hash_table_iter_init (&iter, self->counters);

  while (g_hash_table_iter_next (&iter, &key, &val))
    {
      LineData *ld = val;
      gdouble last_x = 0;
      gdouble last_y = 0;

      cairo_move_to (cr, 0, alloc.height);

      for (i = 0; i < ld->values->len; i++)
        {
          LineDataItem *item = &g_array_index (ld->values, LineDataItem, i);
          gdouble x;
          gdouble y;

          x = calc_x (self, item->time) * alloc.width;
          y = alloc.height - (calc_y (self, ld, item->value) * alloc.height);

          //g_print ("x=%d y =%d\n", (int)x, (int)y);

          cairo_line_to (cr, x, y);

          (void)last_x;
          (void)last_y;

          last_x = x;
          last_y = x;
        }

      cairo_line_to (cr, alloc.width, alloc.height);
      cairo_line_to (cr, 0, alloc.height);
      cairo_set_line_width (cr, 1.0);
      cairo_set_source_rgba (cr, 0, 0, 0, 0.4);
      cairo_stroke_preserve (cr);
      cairo_set_source_rgba (cr, 0, 0, 0, 0.2);
      cairo_fill (cr);
    }

  cairo_restore (cr);

  return ret;
}

static void
sp_line_visualizer_row_set_reader (SpVisualizerRow *row,
                                   SpCaptureReader *reader)
{
  SpLineVisualizerRow *self = (SpLineVisualizerRow *)row;

  g_assert (SP_IS_LINE_VISUALIZER_ROW (self));

  if (self->reader != reader)
    {
      g_clear_pointer (&self->reader, sp_capture_reader_unref);

      if (reader)
      {
        self->reader = sp_capture_reader_ref (reader);
        self->start_time = 0;
        self->duration = 0;
      }

      /* TODO */
      sp_line_visualizer_row_add_counter (self, 1);
      sp_line_visualizer_row_add_counter (self, 3);
      sp_line_visualizer_row_add_counter (self, 5);
      sp_line_visualizer_row_add_counter (self, 7);

      gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}

static void
sp_line_visualizer_row_finalize (GObject *object)
{
  SpLineVisualizerRow *self = (SpLineVisualizerRow *)object;

  g_clear_pointer (&self->counters, g_hash_table_unref);
  g_clear_pointer (&self->reader, sp_capture_reader_unref);

  G_OBJECT_CLASS (sp_line_visualizer_row_parent_class)->finalize (object);
}

static void
sp_line_visualizer_row_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  SpLineVisualizerRow *self = SP_LINE_VISUALIZER_ROW (object);

  switch (prop_id)
    {
    case PROP_TITLE:
      g_object_get_property (G_OBJECT (self->label), "label", value);
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

  switch (prop_id)
    {
    case PROP_TITLE:
      g_object_set_property (G_OBJECT (self->label), "label", value);
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
  PangoAttrList *attrs = NULL;

  self->counters = g_hash_table_new (NULL, NULL);

  attrs = pango_attr_list_new ();
  pango_attr_list_insert (attrs, pango_attr_scale_new (PANGO_SCALE_SMALL * PANGO_SCALE_SMALL));
  self->label = g_object_new (GTK_TYPE_LABEL,
                              "attributes", attrs,
                              "visible", TRUE,
                              "xalign", 0.0f,
                              "yalign", 0.0f,
                              NULL);
  gtk_container_add (GTK_CONTAINER (self), GTK_WIDGET (self->label));
  pango_attr_list_unref (attrs);
}

void
sp_line_visualizer_row_add_counter (SpLineVisualizerRow *self,
                                    guint                counter_id)
{
  g_assert (SP_IS_LINE_VISUALIZER_ROW (self));

  self->needs_recalc = TRUE;
  g_hash_table_insert (self->counters, GSIZE_TO_POINTER (counter_id), NULL);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}
