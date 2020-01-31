/* sysprof-profiler-assistant.c
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

#define G_LOG_DOMAIN "sysprof-profiler-assistant"

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include "config.h"

#include <sysprof.h>

#include "sysprof-platform.h"

#include "sysprof-aid-icon.h"
#include "sysprof-environ-editor.h"
#include "sysprof-model-filter.h"
#include "sysprof-profiler-assistant.h"
#include "sysprof-process-model-row.h"
#include "sysprof-ui-private.h"

#include "sysprof-battery-aid.h"
#include "sysprof-callgraph-aid.h"
#include "sysprof-cpu-aid.h"
#include "sysprof-memory-aid.h"
#include "sysprof-memprof-aid.h"
#include "sysprof-netdev-aid.h"
#include "sysprof-proxy-aid.h"
#include "sysprof-rapl-aid.h"

struct _SysprofProfilerAssistant
{
  GtkBin                parent_instance;

  SysprofProcessModel  *process_model;

  /* Template Objects */
  GtkSwitch            *allow_throttling;
  GtkButton            *record_button;
  GtkEntry             *command_line;
  GtkSearchEntry       *search_entry;
  GtkRevealer          *process_revealer;
  GtkListBox           *process_list_box;
  SysprofEnvironEditor *environ_editor;
  GtkFlowBox           *aid_flow_box;
  GtkSwitch            *whole_system_switch;
  GtkSwitch            *launch_switch;
  GtkSwitch            *inherit_switch;
};

enum {
  START_RECORDING,
  N_SIGNALS
};

static guint signals [N_SIGNALS];

G_DEFINE_TYPE (SysprofProfilerAssistant, sysprof_profiler_assistant, GTK_TYPE_BIN)

/**
 * sysprof_profiler_assistant_new:
 *
 * Create a new #SysprofProfilerAssistant.
 *
 * Returns: (transfer full): a newly created #SysprofProfilerAssistant
 *
 * Since: 3.34
 */
GtkWidget *
sysprof_profiler_assistant_new (void)
{
  return g_object_new (SYSPROF_TYPE_PROFILER_ASSISTANT, NULL);
}

static void
sysprof_profiler_assistant_aid_activated_cb (SysprofProfilerAssistant *self,
                                             SysprofAidIcon           *icon,
                                             GtkFlowBox               *flow_box)
{
  g_assert (SYSPROF_IS_PROFILER_ASSISTANT (self));
  g_assert (SYSPROF_IS_AID_ICON (icon));
  g_assert (GTK_IS_FLOW_BOX (flow_box));

  sysprof_aid_icon_toggle (icon);
}

static GtkWidget *
create_process_row_cb (gpointer item_,
                       gpointer user_data)
{
  SysprofProcessModelItem *item = item_;

  g_assert (SYSPROF_IS_PROCESS_MODEL_ITEM (item));

  return sysprof_process_model_row_new (item);
}

static void
sysprof_profiler_assistant_notify_reveal_child_cb (SysprofProfilerAssistant *self,
                                                   GParamSpec               *pspec,
                                                   GtkRevealer              *revealer)
{
  g_assert (SYSPROF_IS_PROFILER_ASSISTANT (self));
  g_assert (GTK_IS_REVEALER (revealer));

  if (self->process_model == NULL)
    {
      self->process_model = sysprof_process_model_new ();
      gtk_list_box_bind_model (self->process_list_box,
                               G_LIST_MODEL (self->process_model),
                               create_process_row_cb,
                               NULL, NULL);
      sysprof_process_model_reload (self->process_model);
    }
}

static void
sysprof_profiler_assistant_row_activated_cb (SysprofProfilerAssistant *self,
                                             SysprofProcessModelRow   *row,
                                             GtkListBox               *list_box)
{
  g_assert (SYSPROF_PROFILER_ASSISTANT (self));
  g_assert (SYSPROF_IS_PROCESS_MODEL_ROW (row));
  g_assert (GTK_IS_LIST_BOX (list_box));

  sysprof_process_model_row_set_selected (row,
                                          !sysprof_process_model_row_get_selected (row));
}

static void
sysprof_profiler_assistant_command_line_changed_cb (SysprofProfilerAssistant *self,
                                                    GtkEntry                 *entry)
{
  g_auto(GStrv) argv = NULL;
  GtkStyleContext *style_context;
  const gchar *text;
  gint argc;

  g_assert (SYSPROF_IS_PROFILER_ASSISTANT (self));
  g_assert (GTK_IS_ENTRY (entry));

  style_context = gtk_widget_get_style_context (GTK_WIDGET (entry));
  text = gtk_entry_get_text (entry);

  if (text == NULL || text[0] == 0 || g_shell_parse_argv (text, &argc, &argv, NULL))
    gtk_style_context_remove_class (style_context, GTK_STYLE_CLASS_ERROR);
  else
    gtk_style_context_add_class (style_context, GTK_STYLE_CLASS_ERROR);
}

static void
sysprof_profiler_assistant_foreach_cb (GtkWidget       *widget,
                                       SysprofProfiler *profiler)
{
  g_assert (GTK_IS_WIDGET (widget));
  g_assert (SYSPROF_IS_PROFILER (profiler));

  if (SYSPROF_IS_PROCESS_MODEL_ROW (widget) &&
      sysprof_process_model_row_get_selected (SYSPROF_PROCESS_MODEL_ROW (widget)))
    {
      SysprofProcessModelItem *item;
      GPid pid;

      item = sysprof_process_model_row_get_item (SYSPROF_PROCESS_MODEL_ROW (widget));
      pid = sysprof_process_model_item_get_pid (item);

      sysprof_profiler_add_pid (profiler, pid);
    }
  else if (SYSPROF_IS_AID_ICON (widget))
    {
      if (sysprof_aid_icon_is_selected (SYSPROF_AID_ICON (widget)))
        {
          SysprofAid *aid = sysprof_aid_icon_get_aid (SYSPROF_AID_ICON (widget));

          sysprof_aid_prepare (aid, profiler);
        }
    }
}

static void
sysprof_profiler_assistant_record_clicked_cb (SysprofProfilerAssistant *self,
                                              GtkButton                *button)
{
  g_autoptr(SysprofProfiler) profiler = NULL;
  g_autoptr(SysprofCaptureWriter) writer = NULL;
  g_autoptr(SysprofSource) symbols_source = NULL;
#ifdef __linux__
  g_autoptr(SysprofSource) proc_source = NULL;
#endif
  gint fd;

  g_assert (SYSPROF_IS_PROFILER_ASSISTANT (self));
  g_assert (GTK_IS_BUTTON (button));

  gtk_widget_set_sensitive (GTK_WIDGET (self), FALSE);

  /* Setup a writer immediately */
  if (-1 == (fd = sysprof_memfd_create ("[sysprof-capture]")) ||
      !(writer = sysprof_capture_writer_new_from_fd (fd, 0)))
    {
      if (fd != -1)
        close (fd);
      return;
    }

  profiler = sysprof_local_profiler_new ();
  sysprof_profiler_set_writer (profiler, writer);

  /* Add pids to profiler */
  gtk_container_foreach (GTK_CONTAINER (self->process_list_box),
                         (GtkCallback) sysprof_profiler_assistant_foreach_cb,
                         profiler);

  /* Setup whole system profiling */
  sysprof_profiler_set_whole_system (profiler, gtk_switch_get_active (self->whole_system_switch));

  if (gtk_switch_get_active (self->launch_switch))
    {
      g_auto(GStrv) argv = NULL;
      g_auto(GStrv) env = NULL;
      SysprofEnviron *environ_;
      const gchar *command;
      gint argc;

      command = gtk_entry_get_text (self->command_line);
      g_shell_parse_argv (command, &argc, &argv, NULL);

      sysprof_profiler_set_spawn (profiler, TRUE);
      sysprof_profiler_set_spawn_argv (profiler, (const gchar * const *)argv);

      environ_ = sysprof_environ_editor_get_environ (self->environ_editor);
      env = sysprof_environ_get_environ (environ_);
      sysprof_profiler_set_spawn_env (profiler, (const gchar * const *)env);

      sysprof_profiler_set_spawn_inherit_environ (profiler,
                                                  gtk_switch_get_active (self->inherit_switch));
    }

#ifdef __linux__
  /* Always add the proc source */
  proc_source = sysprof_proc_source_new ();
  sysprof_profiler_add_source (profiler, proc_source);
#endif

  if (!gtk_switch_get_active (self->allow_throttling))
    {
      g_autoptr(SysprofSource) governor = sysprof_governor_source_new ();
      sysprof_profiler_add_source (profiler, governor);
    }

  /* Always add symbol decoder to save to file immediately */
  symbols_source = sysprof_symbols_source_new ();
  sysprof_profiler_add_source (profiler, symbols_source);

  /* Now allow the aids to add their sources */
  gtk_container_foreach (GTK_CONTAINER (self->aid_flow_box),
                         (GtkCallback) sysprof_profiler_assistant_foreach_cb,
                         profiler);

  g_signal_emit (self, signals [START_RECORDING], 0, profiler);
}

static gboolean
filter_by_search_text (GObject  *object,
                       gpointer  user_data)
{
  SysprofProcessModelItem *item = SYSPROF_PROCESS_MODEL_ITEM (object);
  const gchar *haystack;
  const gchar * const *argv;
  const gchar *text = user_data;

  haystack = sysprof_process_model_item_get_command_line (item);

  if (haystack)
    {
      if (strcasestr (haystack, text) != NULL)
        return TRUE;
    }

  argv = sysprof_process_model_item_get_argv (item);

  if (argv)
    {
      for (guint i = 0; argv[i]; i++)
        {
          if (strcasestr (argv[i], text) != NULL)
            return TRUE;
        }
    }

  return FALSE;
}

static void
sysprof_profiler_assistant_search_changed_cb (SysprofProfilerAssistant *self,
                                              GtkSearchEntry           *search_entry)
{
  g_autoptr(SysprofModelFilter) filter = NULL;
  const gchar *text;

  g_assert (SYSPROF_IS_PROFILER_ASSISTANT (self));
  g_assert (GTK_IS_SEARCH_ENTRY (search_entry));

  if (self->process_model == NULL)
    return;

  sysprof_process_model_queue_reload (self->process_model);

  text = gtk_entry_get_text (GTK_ENTRY (search_entry));

  if (text[0] == 0)
    {
      gtk_list_box_bind_model (self->process_list_box,
                               G_LIST_MODEL (self->process_model),
                               create_process_row_cb,
                               NULL, NULL);
      return;
    }

  filter = sysprof_model_filter_new (G_LIST_MODEL (self->process_model));
  sysprof_model_filter_set_filter_func (filter,
                                        filter_by_search_text,
                                        g_strdup (text),
                                        g_free);
  gtk_list_box_bind_model (self->process_list_box,
                           G_LIST_MODEL (filter),
                           create_process_row_cb,
                           NULL, NULL);
}

static void
sysprof_profiler_assistant_destroy (GtkWidget *widget)
{
  SysprofProfilerAssistant *self = (SysprofProfilerAssistant *)widget;

  g_clear_object (&self->process_model);

  GTK_WIDGET_CLASS (sysprof_profiler_assistant_parent_class)->destroy (widget);
}

static void
sysprof_profiler_assistant_class_init (SysprofProfilerAssistantClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  widget_class->destroy = sysprof_profiler_assistant_destroy;

  /**
   * SysprofProfilerAssistant::start-recording:
   * @self: a #SysprofProfilerAssistant
   * @profiler: a #SysprofProfiler
   *
   * This signal is emitted when a new profiling session should start.
   */
  signals [START_RECORDING] =
    g_signal_new ("start-recording",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1, SYSPROF_TYPE_PROFILER);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/ui/sysprof-profiler-assistant.ui");
  gtk_widget_class_bind_template_child (widget_class, SysprofProfilerAssistant, allow_throttling);
  gtk_widget_class_bind_template_child (widget_class, SysprofProfilerAssistant, aid_flow_box);
  gtk_widget_class_bind_template_child (widget_class, SysprofProfilerAssistant, command_line);
  gtk_widget_class_bind_template_child (widget_class, SysprofProfilerAssistant, environ_editor);
  gtk_widget_class_bind_template_child (widget_class, SysprofProfilerAssistant, process_list_box);
  gtk_widget_class_bind_template_child (widget_class, SysprofProfilerAssistant, process_revealer);
  gtk_widget_class_bind_template_child (widget_class, SysprofProfilerAssistant, record_button);
  gtk_widget_class_bind_template_child (widget_class, SysprofProfilerAssistant, whole_system_switch);
  gtk_widget_class_bind_template_child (widget_class, SysprofProfilerAssistant, launch_switch);
  gtk_widget_class_bind_template_child (widget_class, SysprofProfilerAssistant, inherit_switch);
  gtk_widget_class_bind_template_child (widget_class, SysprofProfilerAssistant, search_entry);

  g_type_ensure (SYSPROF_TYPE_AID_ICON);
  g_type_ensure (SYSPROF_TYPE_BATTERY_AID);
  g_type_ensure (SYSPROF_TYPE_CALLGRAPH_AID);
  g_type_ensure (SYSPROF_TYPE_CPU_AID);
  g_type_ensure (SYSPROF_TYPE_DISKSTAT_SOURCE);
  g_type_ensure (SYSPROF_TYPE_ENVIRON_EDITOR);
  g_type_ensure (SYSPROF_TYPE_MEMORY_AID);
  g_type_ensure (SYSPROF_TYPE_MEMPROF_AID);
  g_type_ensure (SYSPROF_TYPE_NETDEV_AID);
  g_type_ensure (SYSPROF_TYPE_PROXY_AID);
  g_type_ensure (SYSPROF_TYPE_RAPL_AID);
}

static void
sysprof_profiler_assistant_init (SysprofProfilerAssistant *self)
{
  g_autoptr(SysprofEnviron) environ_ = sysprof_environ_new ();

  gtk_widget_init_template (GTK_WIDGET (self));

  g_signal_connect_object (self->record_button,
                           "clicked",
                           G_CALLBACK (sysprof_profiler_assistant_record_clicked_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->command_line,
                           "changed",
                           G_CALLBACK (sysprof_profiler_assistant_command_line_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->process_list_box,
                           "row-activated",
                           G_CALLBACK (sysprof_profiler_assistant_row_activated_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->process_revealer,
                           "notify::reveal-child",
                           G_CALLBACK (sysprof_profiler_assistant_notify_reveal_child_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->aid_flow_box,
                           "child-activated",
                           G_CALLBACK (sysprof_profiler_assistant_aid_activated_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->search_entry,
                           "changed",
                           G_CALLBACK (sysprof_profiler_assistant_search_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);

  sysprof_environ_editor_set_environ (self->environ_editor, environ_);
}

void
_sysprof_profiler_assistant_focus_record (SysprofProfilerAssistant *self)
{
  g_return_if_fail (SYSPROF_IS_PROFILER_ASSISTANT (self));

  gtk_widget_grab_focus (GTK_WIDGET (self->record_button));
}

void
sysprof_profiler_assistant_set_executable (SysprofProfilerAssistant *self,
                                           const gchar              *path)
{
  g_return_if_fail (SYSPROF_IS_PROFILER_ASSISTANT (self));

  if (path == NULL || path[0] == 0)
    {
      gtk_entry_set_text (GTK_ENTRY (self->command_line), "");
      gtk_switch_set_active (self->launch_switch, FALSE);
    }
  else
    {
      gtk_entry_set_text (GTK_ENTRY (self->command_line), path);
      gtk_switch_set_active (self->launch_switch, TRUE);
      gtk_widget_grab_focus (GTK_WIDGET (self->command_line));
    }
}
