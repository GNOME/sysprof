/* sp-cpu-visualizer-row.c
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sp-cpu-visualizer-row"

#include "config.h"

#include "sp-capture-condition.h"
#include "sp-capture-cursor.h"
#include "sp-color-cycle.h"
#include "sp-cpu-visualizer-row.h"

struct _SpCpuVisualizerRow
{
  SpLineVisualizerRow parent_instance;
  SpColorCycle *colors;
};

G_DEFINE_TYPE (SpCpuVisualizerRow, sp_cpu_visualizer_row, SP_TYPE_LINE_VISUALIZER_ROW)

static gboolean
sp_cpu_visualizer_counter_found (const SpCaptureFrame *frame,
                                 gpointer              user_data)
{
  const SpCaptureFrameCounterDefine *def = (SpCaptureFrameCounterDefine *)frame;
  GArray *counters = user_data;
  gboolean found = FALSE;

  g_assert (frame->type == SP_CAPTURE_FRAME_CTRDEF);

  /*
   * In practice, all the CPU counters are defined at once, so we can avoid
   * walking the rest of the capture by returning after we find our CTRDEF.
   */

  for (guint i = 0; i < def->n_counters; i++)
    {
      if (g_str_equal (def->counters[i].category, "CPU Percent"))
        {
          guint id = def->counters[i].id;
          g_array_append_val (counters, id);
          found = TRUE;
        }
    }

  return !found;
}

static void
sp_cpu_visualizer_row_discover_counters (GTask        *task,
                                         gpointer      source_object,
                                         gpointer      task_data,
                                         GCancellable *canellable)
{
  const SpCaptureFrameType types[] = { SP_CAPTURE_FRAME_CTRDEF };
  SpCaptureReader *reader = task_data;
  g_autoptr(SpCaptureCursor) cursor = NULL;
  g_autoptr(GArray) counters = NULL;

  g_assert (G_IS_TASK (task));
  g_assert (SP_IS_CPU_VISUALIZER_ROW (source_object));
  g_assert (reader != NULL);

  counters = g_array_new (FALSE, FALSE, sizeof (guint));
  cursor = sp_capture_cursor_new (reader);
  sp_capture_cursor_add_condition (cursor, sp_capture_condition_new_where_type_in (G_N_ELEMENTS (types), types));
  sp_capture_cursor_foreach (cursor, sp_cpu_visualizer_counter_found, counters);
  g_task_return_pointer (task, g_steal_pointer (&counters), (GDestroyNotify)g_array_unref);
}

static void
complete_counters (GObject      *object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  SpCpuVisualizerRow *self = (SpCpuVisualizerRow *)object;
  g_autoptr(GArray) counters = NULL;

  g_assert (SP_IS_CPU_VISUALIZER_ROW (self));
  g_assert (G_IS_TASK (result));

  counters = g_task_propagate_pointer (G_TASK (result), NULL);

  if (counters != NULL)
    {
      for (guint i = 0; i < counters->len; i++)
        {
          guint counter_id = g_array_index (counters, guint, i);
          GdkRGBA color;

          sp_color_cycle_next (self->colors, &color);
          sp_line_visualizer_row_add_counter (SP_LINE_VISUALIZER_ROW (self), counter_id, &color);
        }
    }

  /* Hide ourself if we failed to locate counters */
  gtk_widget_set_visible (GTK_WIDGET (self), counters != NULL && counters->len > 0);
}

static void
sp_cpu_visualizer_row_set_reader (SpVisualizerRow *row,
                                  SpCaptureReader *reader)
{
  SpCpuVisualizerRow *self = (SpCpuVisualizerRow *)row;
  g_autoptr(GTask) task = NULL;

  g_assert (SP_IS_CPU_VISUALIZER_ROW (row));

  sp_color_cycle_reset (self->colors);

  sp_line_visualizer_row_clear (SP_LINE_VISUALIZER_ROW (row));

  SP_VISUALIZER_ROW_CLASS (sp_cpu_visualizer_row_parent_class)->set_reader (row, reader);

  if (reader != NULL)
    {
      task = g_task_new (self, NULL, complete_counters, NULL);
      g_task_set_source_tag (task, sp_cpu_visualizer_row_set_reader);
      g_task_set_task_data (task, sp_capture_reader_copy (reader),
                            (GDestroyNotify)sp_capture_reader_unref);
      g_task_run_in_thread (task, sp_cpu_visualizer_row_discover_counters);
    }
}

static void
sp_cpu_visualizer_row_finalize (GObject *object)
{
  SpCpuVisualizerRow *self = (SpCpuVisualizerRow *)object;

  g_clear_pointer (&self->colors, sp_color_cycle_unref);

  G_OBJECT_CLASS (sp_cpu_visualizer_row_parent_class)->finalize (object);
}

static void
sp_cpu_visualizer_row_class_init (SpCpuVisualizerRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SpVisualizerRowClass *row_class = SP_VISUALIZER_ROW_CLASS (klass);

  object_class->finalize = sp_cpu_visualizer_row_finalize;

  row_class->set_reader = sp_cpu_visualizer_row_set_reader;
}

static void
sp_cpu_visualizer_row_init (SpCpuVisualizerRow *self)
{
  self->colors = sp_color_cycle_new ();
}

GtkWidget *
sp_cpu_visualizer_row_new (void)
{
  return g_object_new (SP_TYPE_CPU_VISUALIZER_ROW, NULL);
}
