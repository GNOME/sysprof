/* sysprof-cpu-aid.c
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

#define G_LOG_DOMAIN "sysprof-cpu-aid"

#include "config.h"

#include <glib/gi18n.h>

#include "sysprof-color-cycle.h"
#include "sysprof-cpu-aid.h"
#include "sysprof-line-visualizer.h"
#include "sysprof-procs-visualizer.h"

struct _SysprofCpuAid
{
  SysprofAid parent_instance;
};

typedef struct
{
  SysprofCaptureCursor *cursor;
  SysprofDisplay       *display;
  GArray               *counters;
  guint                 has_processes : 1;
} Present;

G_DEFINE_TYPE (SysprofCpuAid, sysprof_cpu_aid, SYSPROF_TYPE_AID)

static void
present_free (gpointer data)
{
  Present *p = data;

  g_clear_pointer (&p->cursor, sysprof_capture_cursor_unref);
  g_clear_pointer (&p->counters, g_array_unref);
  g_clear_object (&p->display);
  g_slice_free (Present, p);
}

/**
 * sysprof_cpu_aid_new:
 *
 * Create a new #SysprofCpuAid.
 *
 * Returns: (transfer full): a newly created #SysprofCpuAid
 *
 * Since: 3.34
 */
SysprofAid *
sysprof_cpu_aid_new (void)
{
  return g_object_new (SYSPROF_TYPE_CPU_AID, NULL);
}

static void
sysprof_cpu_aid_prepare (SysprofAid      *self,
                         SysprofProfiler *profiler)
{
#ifdef __linux__
  g_autoptr(SysprofSource) source = NULL;

  g_assert (SYSPROF_IS_CPU_AID (self));
  g_assert (SYSPROF_IS_PROFILER (profiler));

  source = sysprof_hostinfo_source_new ();
  sysprof_profiler_add_source (profiler, source);
#endif
}

static bool
collect_info (const SysprofCaptureFrame *frame,
              gpointer                   user_data)
{
  SysprofCaptureCounterDefine *def = (SysprofCaptureCounterDefine *)frame;
  Present *p = user_data;

  g_assert (frame != NULL);
  g_assert (p != NULL);
  g_assert (p->counters != NULL);

  if (frame->type == SYSPROF_CAPTURE_FRAME_CTRDEF)
    {
      for (guint i = 0; i < def->n_counters; i++)
        {
          const SysprofCaptureCounter *counter = &def->counters[i];

          if (g_strcmp0 (counter->category, "CPU Percent") == 0 ||
              g_strcmp0 (counter->category, "CPU Frequency") == 0)
            g_array_append_vals (p->counters, counter, 1);
        }
    }
  else if (!p->has_processes &&
           (frame->type == SYSPROF_CAPTURE_FRAME_PROCESS ||
            frame->type == SYSPROF_CAPTURE_FRAME_EXIT))
    {
      p->has_processes = TRUE;
    }

  return TRUE;
}

static void
sysprof_cpu_aid_present_worker (GTask        *task,
                                gpointer      source_object,
                                gpointer      task_data,
                                GCancellable *cancellable)
{
  Present *present = task_data;

  g_assert (G_IS_TASK (task));
  g_assert (SYSPROF_IS_CPU_AID (source_object));
  g_assert (present != NULL);
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  sysprof_capture_cursor_foreach (present->cursor, collect_info, present);
  g_task_return_pointer (task,
                         g_steal_pointer (&present->counters),
                         (GDestroyNotify) g_array_unref);
}

static void
sysprof_cpu_aid_present_async (SysprofAid           *aid,
                               SysprofCaptureReader *reader,
                               SysprofDisplay       *display,
                               GCancellable         *cancellable,
                               GAsyncReadyCallback   callback,
                               gpointer              user_data)
{
  static const SysprofCaptureFrameType types[] = {
    SYSPROF_CAPTURE_FRAME_CTRDEF,
    SYSPROF_CAPTURE_FRAME_PROCESS,
    SYSPROF_CAPTURE_FRAME_EXIT,
  };
  g_autoptr(SysprofCaptureCondition) condition = NULL;
  g_autoptr(SysprofCaptureCursor) cursor = NULL;
  g_autoptr(GTask) task = NULL;
  Present present;

  g_assert (SYSPROF_IS_CPU_AID (aid));
  g_assert (reader != NULL);
  g_assert (SYSPROF_IS_DISPLAY (display));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  condition = sysprof_capture_condition_new_where_type_in (G_N_ELEMENTS (types), types);
  cursor = sysprof_capture_cursor_new (reader);
  sysprof_capture_cursor_add_condition (cursor, g_steal_pointer (&condition));

  present.cursor = g_steal_pointer (&cursor);
  present.display = g_object_ref (display);
  present.counters = g_array_new (FALSE, FALSE, sizeof (SysprofCaptureCounter));
  present.has_processes = FALSE;

  task = g_task_new (aid, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_cpu_aid_present_async);
  g_task_set_task_data (task,
                        g_slice_dup (Present, &present),
                        present_free);
  g_task_run_in_thread (task, sysprof_cpu_aid_present_worker);
}

static gboolean
sysprof_cpu_aid_present_finish (SysprofAid    *aid,
                                GAsyncResult  *result,
                                GError       **error)
{
  g_autoptr(GArray) counters = NULL;
  Present *present;

  g_assert (SYSPROF_IS_AID (aid));
  g_assert (G_IS_TASK (result));

  present = g_task_get_task_data (G_TASK (result));

  if ((counters = g_task_propagate_pointer (G_TASK (result), error)))
    {
      g_autoptr(SysprofColorCycle) cycle = sysprof_color_cycle_new ();
      g_autoptr(SysprofColorCycle) freq_cycle = sysprof_color_cycle_new ();
      SysprofVisualizerGroup *usage;
      SysprofVisualizerGroup *freq;
      SysprofVisualizer *freq_row = NULL;
      SysprofVisualizer *over_row = NULL;
      gboolean found_combined = FALSE;
      gboolean has_usage = FALSE;
      gboolean has_freq = FALSE;

      usage = g_object_new (SYSPROF_TYPE_VISUALIZER_GROUP,
                            "can-focus", TRUE,
                            "priority", -1000,
                            "title", _("CPU Usage"),
                            "visible", TRUE,
                            NULL);

      freq = g_object_new (SYSPROF_TYPE_VISUALIZER_GROUP,
                           "can-focus", TRUE,
                            "priority", -999,
                           "title", _("CPU Frequency"),
                           "visible", TRUE,
                           NULL);
      freq_row = g_object_new (SYSPROF_TYPE_LINE_VISUALIZER,
                               "title", _("CPU Frequency (All)"),
                               "height-request", 35,
                               "visible", TRUE,
                               "y-lower", 0.0,
                               "y-upper", 100.0,
                               NULL);
      gtk_container_add (GTK_CONTAINER (freq), GTK_WIDGET (freq_row));

      over_row = g_object_new (SYSPROF_TYPE_LINE_VISUALIZER,
                               "title", _("CPU Usage (All)"),
                               "height-request", 35,
                               "visible", TRUE,
                               "y-lower", 0.0,
                               "y-upper", 100.0,
                               NULL);

      for (guint i = 0; i < counters->len; i++)
        {
          const SysprofCaptureCounter *ctr = &g_array_index (counters, SysprofCaptureCounter, i);

          if (g_strcmp0 (ctr->category, "CPU Percent") == 0)
            {
              if (strstr (ctr->name, "Combined") != NULL)
                {
                  GtkWidget *row;
                  GdkRGBA rgba;

                  found_combined = TRUE;

                  gdk_rgba_parse (&rgba, "#1a5fb4");
                  row = g_object_new (SYSPROF_TYPE_LINE_VISUALIZER,
                                      /* Translators: CPU is the processor. */
                                      "title", _("CPU Usage (All)"),
                                      "height-request", 35,
                                      "visible", TRUE,
                                      "y-lower", 0.0,
                                      "y-upper", 100.0,
                                      NULL);
                  sysprof_line_visualizer_add_counter (SYSPROF_LINE_VISUALIZER (row), ctr->id, &rgba);
                  rgba.alpha = 0.5;
                  sysprof_line_visualizer_set_fill (SYSPROF_LINE_VISUALIZER (row), ctr->id, &rgba);
                  sysprof_visualizer_group_insert (usage, SYSPROF_VISUALIZER (row), 0, FALSE);
                  has_usage = TRUE;
                }
              else if (g_str_has_prefix (ctr->name, "Total CPU "))
                {
                  GtkWidget *row;
                  GdkRGBA rgba;

                  sysprof_color_cycle_next (cycle, &rgba);
                  row = g_object_new (SYSPROF_TYPE_LINE_VISUALIZER,
                                      "title", ctr->name,
                                      "height-request", 35,
                                      "visible", FALSE,
                                      "y-lower", 0.0,
                                      "y-upper", 100.0,
                                      NULL);
                  sysprof_line_visualizer_add_counter (SYSPROF_LINE_VISUALIZER (row), ctr->id, &rgba);
                  sysprof_line_visualizer_add_counter (SYSPROF_LINE_VISUALIZER (over_row), ctr->id, &rgba);
                  rgba.alpha = 0.5;
                  sysprof_line_visualizer_set_fill (SYSPROF_LINE_VISUALIZER (row), ctr->id, &rgba);
                  sysprof_visualizer_group_insert (usage, SYSPROF_VISUALIZER (row), -1, TRUE);
                  has_usage = TRUE;
                }
            }
          else if (g_strcmp0 (ctr->category, "CPU Frequency") == 0)
            {
              if (g_str_has_prefix (ctr->name, "CPU "))
                {
                  g_autofree gchar *title = g_strdup_printf ("%s Frequency", ctr->name);
                  GtkWidget *row;
                  GdkRGBA rgba;

                  sysprof_color_cycle_next (freq_cycle, &rgba);
                  sysprof_line_visualizer_add_counter (SYSPROF_LINE_VISUALIZER (freq_row), ctr->id, &rgba);
                  sysprof_line_visualizer_set_dash (SYSPROF_LINE_VISUALIZER (freq_row), ctr->id, TRUE);

                  row = g_object_new (SYSPROF_TYPE_LINE_VISUALIZER,
                                      "title", title,
                                      "height-request", 35,
                                      "visible", FALSE,
                                      "y-lower", 0.0,
                                      "y-upper", 100.0,
                                      NULL);
                  sysprof_line_visualizer_add_counter (SYSPROF_LINE_VISUALIZER (row), ctr->id, &rgba);
                  sysprof_line_visualizer_set_dash (SYSPROF_LINE_VISUALIZER (row), ctr->id, TRUE);
                  sysprof_visualizer_group_insert (freq, SYSPROF_VISUALIZER (row), -1, TRUE);

                  has_freq = TRUE;
                }
            }
        }

      if (present->has_processes)
        {
          GtkWidget *row;

          row = g_object_new (SYSPROF_TYPE_PROCS_VISUALIZER,
                              "title", _("Processes"),
                              "height-request", 35,
                              "visible", FALSE,
                              NULL);
          sysprof_visualizer_group_insert (usage, SYSPROF_VISUALIZER (row), -1, TRUE);
        }

      if (has_usage && !found_combined)
        sysprof_visualizer_group_insert (usage, over_row, 0, FALSE);
      else
        gtk_widget_destroy (GTK_WIDGET (over_row));

      if (has_usage)
        sysprof_display_add_group (present->display, usage);
      else
        gtk_widget_destroy (GTK_WIDGET (usage));

      if (has_freq)
        sysprof_display_add_group (present->display, freq);
      else
        gtk_widget_destroy (GTK_WIDGET (freq));
    }

  return counters != NULL;
}

static void
sysprof_cpu_aid_class_init (SysprofCpuAidClass *klass)
{
  SysprofAidClass *aid_class = SYSPROF_AID_CLASS (klass);

  aid_class->prepare = sysprof_cpu_aid_prepare;
  aid_class->present_async = sysprof_cpu_aid_present_async;
  aid_class->present_finish = sysprof_cpu_aid_present_finish;
}

static void
sysprof_cpu_aid_init (SysprofCpuAid *self)
{
  sysprof_aid_set_display_name (SYSPROF_AID (self), _("CPU Usage"));
  sysprof_aid_set_icon_name (SYSPROF_AID (self), "org.gnome.Sysprof-symbolic");
}
