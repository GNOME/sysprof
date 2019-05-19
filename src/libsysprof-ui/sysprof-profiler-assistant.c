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

#include "config.h"

#include <sysprof.h>

#include "sysprof-aid-icon.h"
#include "sysprof-cpu-aid.h"
#include "sysprof-environ-editor.h"
#include "sysprof-profiler-assistant.h"
#include "sysprof-process-model-row.h"

struct _SysprofProfilerAssistant
{
  GtkBin                parent_instance;

  /* Template Objects */
  GtkEntry             *command_line;
  GtkRevealer          *process_revealer;
  GtkListBox           *process_list_box;
  SysprofEnvironEditor *environ_editor;
  GtkFlowBox           *aid_flow_box;
};

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

  if (gtk_revealer_get_reveal_child (revealer))
    {
      g_autoptr(SysprofProcessModel) model = NULL;

      model = sysprof_process_model_new ();
      gtk_list_box_bind_model (self->process_list_box,
                               G_LIST_MODEL (model),
                               create_process_row_cb,
                               NULL, NULL);
      sysprof_process_model_reload (model);
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
sysprof_profiler_assistant_class_init (SysprofProfilerAssistantClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/ui/sysprof-profiler-assistant.ui");
  gtk_widget_class_bind_template_child (widget_class, SysprofProfilerAssistant, aid_flow_box);
  gtk_widget_class_bind_template_child (widget_class, SysprofProfilerAssistant, command_line);
  gtk_widget_class_bind_template_child (widget_class, SysprofProfilerAssistant, environ_editor);
  gtk_widget_class_bind_template_child (widget_class, SysprofProfilerAssistant, process_list_box);
  gtk_widget_class_bind_template_child (widget_class, SysprofProfilerAssistant, process_revealer);

  g_type_ensure (SYSPROF_TYPE_AID_ICON);
  g_type_ensure (SYSPROF_TYPE_CPU_AID);
  g_type_ensure (SYSPROF_TYPE_ENVIRON_EDITOR);
}

static void
sysprof_profiler_assistant_init (SysprofProfilerAssistant *self)
{
  g_autoptr(SysprofEnviron) environ = sysprof_environ_new ();

  gtk_widget_init_template (GTK_WIDGET (self));

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

  sysprof_environ_editor_set_environ (self->environ_editor, environ);
}
