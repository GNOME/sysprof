/* sysprof-greeter.c
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

#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <sysprof-capture.h>
#include <sysprof-profiler.h>

#include "sysprof-greeter.h"
#include "sysprof-recording-pad.h"

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureWriter, sysprof_capture_writer_unref)

struct _SysprofGreeter
{
  AdwWindow           parent_instance;

  GFile              *file;

  AdwViewStack       *view_stack;
  GtkBox             *toolbar;
  AdwPreferencesPage *record_page;
  GtkWidget          *open_page;
  GtkSwitch          *sample_native_stacks;
  GtkSwitch          *sample_javascript_stacks;
  GtkSwitch          *record_disk_usage;
  GtkSwitch          *record_network_usage;
  GtkSwitch          *record_compositor;
  GtkSwitch          *record_system_logs;
  GtkSwitch          *record_session_bus;
  GtkSwitch          *record_system_bus;
  GtkSwitch          *bundle_symbols;
};

enum {
  PROP_0,
  PROP_FILE,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofGreeter, sysprof_greeter, ADW_TYPE_WINDOW)

static GParamSpec *properties [N_PROPS];

static void
sysprof_greeter_view_stack_notify_visible_child (SysprofGreeter *self,
                                                 GParamSpec     *pspec,
                                                 AdwViewStack   *stack)
{
  g_assert (SYSPROF_IS_GREETER (self));
  g_assert (ADW_IS_VIEW_STACK (stack));

  gtk_widget_set_visible (GTK_WIDGET (self->toolbar),
                          GTK_WIDGET (self->record_page) == adw_view_stack_get_visible_child (stack));
}

static SysprofProfiler *
sysprof_greeter_create_profiler (SysprofGreeter *self)
{
  g_autoptr(SysprofProfiler) profiler = NULL;
  g_autoptr(SysprofSpawnable) spawnable = NULL;

  g_assert (SYSPROF_IS_GREETER (self));

  profiler = sysprof_profiler_new ();

  /* TODO: Setup spawnable */

  if (gtk_switch_get_active (self->sample_native_stacks))
    sysprof_profiler_add_instrument (profiler, sysprof_sampler_new ());

  if (gtk_switch_get_active (self->sample_javascript_stacks))
    {
      if (spawnable != NULL)
        sysprof_profiler_add_instrument (profiler,
                                         sysprof_tracefd_consumer_new (sysprof_spawnable_add_trace_fd (spawnable,
                                                                                                       "GJS_TRACE_FD")));
    }

  if (gtk_switch_get_active (self->record_disk_usage))
    sysprof_profiler_add_instrument (profiler, sysprof_disk_usage_new ());

  if (gtk_switch_get_active (self->record_network_usage))
    sysprof_profiler_add_instrument (profiler, sysprof_network_usage_new ());

  if (gtk_switch_get_active (self->record_compositor))
    sysprof_profiler_add_instrument (profiler,
                                     sysprof_proxied_instrument_new (G_BUS_TYPE_SESSION,
                                                                     "org.gnome.Shell",
                                                                     "/org/gnome/Sysprof3/Profiler"));

  if (gtk_switch_get_active (self->record_system_logs))
    sysprof_profiler_add_instrument (profiler, sysprof_system_logs_new ());

  if (gtk_switch_get_active (self->bundle_symbols))
    sysprof_profiler_add_instrument (profiler, sysprof_symbols_bundle_new ());

  if (gtk_switch_get_active (self->record_session_bus))
    sysprof_profiler_add_instrument (profiler, sysprof_dbus_monitor_new (G_BUS_TYPE_SESSION));

  if (gtk_switch_get_active (self->record_system_bus))
    sysprof_profiler_add_instrument (profiler, sysprof_dbus_monitor_new (G_BUS_TYPE_SYSTEM));

  return g_steal_pointer (&profiler);
}

static void
sysprof_greeter_record_cb (GObject      *object,
                           GAsyncResult *result,
                           gpointer      user_data)
{
  SysprofProfiler *profiler = (SysprofProfiler *)object;
  g_autoptr(SysprofRecording) recording = NULL;
  g_autoptr(SysprofGreeter) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (SYSPROF_IS_PROFILER (profiler));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (SYSPROF_IS_GREETER (self));

  if (!(recording = sysprof_profiler_record_finish (profiler, result, &error)))
    {
      /* TODO: error message */
    }
  else
    {
      GtkWidget *pad = sysprof_recording_pad_new (recording);

      gtk_window_present (GTK_WINDOW (pad));
    }

  gtk_window_destroy (GTK_WINDOW (self));
}

static void
sysprof_greeter_record_to_memory_action (GtkWidget  *widget,
                                         const char *action_name,
                                         GVariant   *param)
{
  SysprofGreeter *self = (SysprofGreeter *)widget;
  g_autoptr(SysprofCaptureWriter) writer = NULL;
  g_autoptr(SysprofProfiler) profiler = NULL;
  g_autofd int fd = -1;

  g_assert (SYSPROF_IS_GREETER (self));

  fd = sysprof_memfd_create ("[sysprof-profile]");

  profiler = sysprof_greeter_create_profiler (self);
  writer = sysprof_capture_writer_new_from_fd (g_steal_fd (&fd), 0);

  gtk_widget_set_sensitive (GTK_WIDGET (self), FALSE);

  sysprof_profiler_record_async (profiler,
                                 writer,
                                 NULL,
                                 sysprof_greeter_record_cb,
                                 g_object_ref (self));
}

static void
sysprof_greeter_choose_file_for_record_cb (GObject      *object,
                                           GAsyncResult *result,
                                           gpointer      user_data)
{
  GtkFileDialog *dialog = (GtkFileDialog *)object;
  g_autoptr(SysprofGreeter) self = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GFile) file = NULL;

  g_assert (GTK_IS_FILE_DIALOG (dialog));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (SYSPROF_IS_GREETER (self));

  if ((file = gtk_file_dialog_save_finish (dialog, result, &error)))
    {
      if (g_file_is_native (file))
        {
          g_autoptr(SysprofCaptureWriter) writer = NULL;
          g_autoptr(SysprofProfiler) profiler = NULL;

          profiler = sysprof_greeter_create_profiler (self);
          writer = sysprof_capture_writer_new (g_file_peek_path (file), 0);

          gtk_widget_set_sensitive (GTK_WIDGET (self), FALSE);

          sysprof_profiler_record_async (profiler,
                                         writer,
                                         NULL,
                                         sysprof_greeter_record_cb,
                                         g_object_ref (self));
        }
      else
        {
          GtkWidget *message;

          message = adw_message_dialog_new (NULL,
                                            _("Must Capture to Local File"),
                                            _("You must choose a local file to capture using Sysprof"));
          adw_message_dialog_add_response (ADW_MESSAGE_DIALOG (message), "close", _("Close"));
          gtk_window_present (GTK_WINDOW (message));
        }
    }
}

static void
sysprof_greeter_record_to_file_action (GtkWidget  *widget,
                                       const char *action_name,
                                       GVariant   *param)
{
  SysprofGreeter *self = (SysprofGreeter *)widget;
  g_autoptr(GtkFileDialog) dialog = NULL;
  g_autoptr(GDateTime) now = NULL;
  g_autofree char *now_str = NULL;
  g_autofree char *initial_name = NULL;

  g_assert (SYSPROF_IS_GREETER (self));

  now = g_date_time_new_now_local ();
  now_str = g_date_time_format (now, "%Y-%m-%d %H:%M:%S");
  initial_name = g_strdup_printf (_("System Capture from %s.syscap"), now_str);
  g_strdelimit (initial_name, G_DIR_SEPARATOR_S, '-');

  dialog = gtk_file_dialog_new ();
  gtk_file_dialog_set_title (dialog, _("Record to File"));
  gtk_file_dialog_set_accept_label (dialog, _("Record"));
  gtk_file_dialog_set_modal (dialog, TRUE);
  gtk_file_dialog_set_initial_name (dialog, initial_name);

  gtk_file_dialog_save (dialog,
                        GTK_WINDOW (self),
                        NULL,
                        sysprof_greeter_choose_file_for_record_cb,
                        g_object_ref (self));
}

static void
sysprof_greeter_choose_file_for_open_cb (GObject      *object,
                                         GAsyncResult *result,
                                         gpointer      user_data)
{
  GtkFileDialog *dialog = (GtkFileDialog *)object;
  g_autoptr(SysprofGreeter) self = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GFile) file = NULL;

  g_assert (GTK_IS_FILE_DIALOG (dialog));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (SYSPROF_IS_GREETER (self));

  if ((file = gtk_file_dialog_open_finish (dialog, result, &error)))
    {
      if (g_file_is_native (file))
        {
          if (g_set_object (&self->file, file))
            g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILE]);
        }
      else
        {
          GtkWidget *message;

          message = adw_message_dialog_new (NULL,
                                            _("Must Capture to Local File"),
                                            _("You must choose a local file to capture using Sysprof"));
          adw_message_dialog_add_response (ADW_MESSAGE_DIALOG (message), "close", _("Close"));
          gtk_window_present (GTK_WINDOW (message));
        }
    }
}

static void
sysprof_greeter_select_file_action (GtkWidget  *widget,
                                    const char *action_name,
                                    GVariant   *param)
{
  SysprofGreeter *self = (SysprofGreeter *)widget;
  g_autoptr(GtkFileDialog) dialog = NULL;
  g_autoptr(GtkFileFilter) filter = NULL;
  g_autoptr(GListStore) filters = NULL;

  g_assert (SYSPROF_IS_GREETER (self));

  dialog = gtk_file_dialog_new ();
  gtk_file_dialog_set_title (dialog, _("Open Recording"));
  gtk_file_dialog_set_accept_label (dialog, _("Open"));
  gtk_file_dialog_set_modal (dialog, TRUE);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Sysprof Capture (*.syscap)"));
  gtk_file_filter_add_mime_type (filter, "application/x-sysprof-capture");
  gtk_file_filter_add_suffix (filter, "syscap");

  filters = g_list_store_new (GTK_TYPE_FILE_FILTER);
  g_list_store_append (filters, filter);
  gtk_file_dialog_set_filters (dialog, G_LIST_MODEL (filters));

  if (self->file)
    gtk_file_dialog_set_initial_file (dialog, self->file);

  gtk_file_dialog_open (dialog,
                        GTK_WINDOW (self),
                        NULL,
                        sysprof_greeter_choose_file_for_open_cb,
                        g_object_ref (self));
}

static char *
get_file_path (gpointer  unused,
               GFile    *file)
{
  if (file)
    return g_file_get_path (file);

  return NULL;
}

static void
sysprof_greeter_dispose (GObject *object)
{
  SysprofGreeter *self = (SysprofGreeter *)object;

  gtk_widget_dispose_template (GTK_WIDGET (self), SYSPROF_TYPE_GREETER);

  G_OBJECT_CLASS (sysprof_greeter_parent_class)->dispose (object);
}

static void
sysprof_greeter_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  SysprofGreeter *self = SYSPROF_GREETER (object);

  switch (prop_id)
    {
    case PROP_FILE:
      g_value_set_object (value, self->file);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_greeter_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  SysprofGreeter *self = SYSPROF_GREETER (object);

  switch (prop_id)
    {
    case PROP_FILE:
      g_set_object (&self->file, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_greeter_class_init (SysprofGreeterClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_greeter_dispose;
  object_class->get_property = sysprof_greeter_get_property;
  object_class->set_property = sysprof_greeter_set_property;

  properties [PROP_FILE] =
    g_param_spec_object ("file", NULL, NULL,
                         G_TYPE_FILE,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-greeter.ui");

  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, bundle_symbols);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, open_page);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, record_compositor);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, record_disk_usage);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, record_network_usage);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, record_page);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, record_session_bus);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, record_system_bus);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, record_system_logs);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, sample_native_stacks);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, sample_javascript_stacks);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, toolbar);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, view_stack);

  gtk_widget_class_bind_template_callback (widget_class, sysprof_greeter_view_stack_notify_visible_child);
  gtk_widget_class_bind_template_callback (widget_class, get_file_path);

  gtk_widget_class_install_action (widget_class, "win.record-to-memory", NULL, sysprof_greeter_record_to_memory_action);
  gtk_widget_class_install_action (widget_class, "win.record-to-file", NULL, sysprof_greeter_record_to_file_action);
  gtk_widget_class_install_action (widget_class, "win.select-file", NULL, sysprof_greeter_select_file_action);
}

static void
sysprof_greeter_init (SysprofGreeter *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
sysprof_greeter_new (void)
{
  return g_object_new (SYSPROF_TYPE_GREETER, NULL);
}

void
sysprof_greeter_set_page (SysprofGreeter     *self,
                          SysprofGreeterPage  page)
{
  g_return_if_fail (SYSPROF_IS_GREETER (self));

  switch (page)
    {
    case SYSPROF_GREETER_PAGE_OPEN:
      adw_view_stack_set_visible_child (self->view_stack, GTK_WIDGET (self->open_page));
      break;

    default:
    case SYSPROF_GREETER_PAGE_RECORD:
      adw_view_stack_set_visible_child (self->view_stack, GTK_WIDGET (self->record_page));
      break;
    }
}
