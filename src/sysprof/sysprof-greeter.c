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

#include "sysprof-entry-popover.h"
#include "sysprof-greeter.h"
#include "sysprof-power-profiles.h"
#include "sysprof-recording-pad.h"
#include "sysprof-recording-template.h"
#include "sysprof-window.h"

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureWriter, sysprof_capture_writer_unref)

struct _SysprofGreeter
{
  AdwWindow                 parent_instance;

  GtkStringList            *envvars;

  AdwViewStack             *view_stack;
  GtkListBox               *sidebar_list_box;
  AdwPreferencesPage       *record_page;
  GtkListBox               *app_environment;
  GtkSwitch                *sample_native_stacks;
  GtkSwitch                *sample_javascript_stacks;
  GtkSwitch                *record_disk_usage;
  GtkSwitch                *record_network_usage;
  GtkSwitch                *record_compositor;
  GtkSwitch                *record_system_logs;
  GtkSwitch                *record_session_bus;
  GtkSwitch                *record_system_bus;
  GtkSwitch                *bundle_symbols;
  GtkButton                *record_to_memory;
  AdwComboRow              *power_combo;
  SysprofRecordingTemplate *recording_template;
};

G_DEFINE_FINAL_TYPE (SysprofGreeter, sysprof_greeter, ADW_TYPE_WINDOW)

#define STRV_INIT(...) (const char * const[]){__VA_ARGS__,NULL}

static inline gboolean
str_empty0 (const char *str)
{
  return str == NULL || str[0] == 0;
}

static void
on_env_items_changed_cb (SysprofGreeter *self,
                         guint           position,
                         guint           removed,
                         guint           added,
                         GListModel     *model)
{
  g_assert (SYSPROF_IS_GREETER (self));
  g_assert (G_IS_LIST_MODEL (model));

  gtk_widget_set_visible (GTK_WIDGET (self->app_environment),
                          g_list_model_get_n_items (model) > 0);
}

static void
on_env_entry_changed_cb (SysprofGreeter      *self,
                         SysprofEntryPopover *popover)
{
  const char *errstr = NULL;
  gboolean valid = FALSE;
  const char *text;
  const char *eq;

  g_assert (SYSPROF_IS_GREETER (self));
  g_assert (SYSPROF_IS_ENTRY_POPOVER (popover));

  text = sysprof_entry_popover_get_text (popover);
  eq = strchr (text, '=');

  if (!str_empty0 (text) && eq == NULL)
    errstr = _("Use KEY=VALUE to set an environment variable");

  if (eq != NULL && eq != text)
    {
      if (g_unichar_isdigit (g_utf8_get_char (text)))
        {
          errstr = _("Keys may not start with a number");
          goto failure;

        }
      for (const char *iter = text; iter < eq; iter = g_utf8_next_char (iter))
        {
          gunichar ch = g_utf8_get_char (iter);

          if (!g_unichar_isalnum (ch) && ch != '_')
            {
              errstr = _("Keys may only contain alpha-numerics or underline.");
              goto failure;
            }
        }

      if (g_ascii_isalpha (*text))
        valid = TRUE;
    }

failure:
  sysprof_entry_popover_set_ready (popover, valid);
  sysprof_entry_popover_set_message (popover, errstr);
}

static void
on_env_entry_activate_cb (SysprofGreeter      *self,
                          const char          *text,
                          SysprofEntryPopover *popover)
{
  g_assert (SYSPROF_IS_GREETER (self));
  g_assert (SYSPROF_IS_ENTRY_POPOVER (popover));
  g_assert (GTK_IS_STRING_LIST (self->envvars));

  gtk_string_list_append (self->envvars, text);
  sysprof_entry_popover_set_text (popover, "");
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

static SysprofProfiler *
sysprof_greeter_create_profiler (SysprofGreeter  *self,
                                 GError         **error)
{
  g_autoptr(SysprofProfiler) profiler = NULL;
  GtkStringObject *strobj;
  const char *str;

  g_assert (SYSPROF_IS_GREETER (self));

  if ((strobj = adw_combo_row_get_selected_item (self->power_combo)) &&
      (str = gtk_string_object_get_string (strobj)))
    g_object_set (self->recording_template,
                  "power-profile", str,
                  NULL);

  if (!(profiler = sysprof_recording_template_apply (self->recording_template, error)))
    return NULL;

  return g_steal_pointer (&profiler);
}

static void
sysprof_greeter_record_to_memory_action (GtkWidget  *widget,
                                         const char *action_name,
                                         GVariant   *param)
{
  SysprofGreeter *self = (SysprofGreeter *)widget;
  g_autoptr(SysprofCaptureWriter) writer = NULL;
  g_autoptr(SysprofProfiler) profiler = NULL;
  g_autoptr(GError) error = NULL;
  g_autofd int fd = -1;

  g_assert (SYSPROF_IS_GREETER (self));

  fd = sysprof_memfd_create ("[sysprof-profile]");

  /* TODO: Handle recording error */
  if (!(profiler = sysprof_greeter_create_profiler (self, &error)))
    return;

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

          /* TODO: Handle recording error */
          if (!(profiler = sysprof_greeter_create_profiler (self, &error)))
            return;

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
sysprof_greeter_select_file_action (GtkWidget  *widget,
                                    const char *action_name,
                                    GVariant   *param)
{
  sysprof_window_open_file (GTK_WINDOW (widget));
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
sidebar_row_activated_cb (SysprofGreeter *self,
                          GtkListBoxRow  *row,
                          GtkListBox     *list_box)
{
  AdwViewStackPage *page = g_object_get_data (G_OBJECT (row), "GREETER_PAGE");

  adw_view_stack_set_visible_child (self->view_stack,
                                    adw_view_stack_page_get_child (page));
}

static GtkWidget *
sysprof_greeter_create_sidebar_row (gpointer item,
                                    gpointer user_data)
{
  AdwViewStackPage *page = item;
  GtkLabel *label;
  GtkBox *box;
  GtkImage *image;
  GtkWidget *row;

  g_assert (ADW_IS_VIEW_STACK_PAGE (page));

  box = g_object_new (GTK_TYPE_BOX,
                      "spacing", 6,
                      NULL);
  image = g_object_new (GTK_TYPE_IMAGE,
                        "icon-name", adw_view_stack_page_get_icon_name (page),
                        NULL);
  label = g_object_new (GTK_TYPE_LABEL,
                        "label", adw_view_stack_page_get_title (page),
                        "use-underline", TRUE,
                        "xalign", .0f,
                        NULL);
  gtk_box_append (box, GTK_WIDGET (image));
  gtk_box_append (box, GTK_WIDGET (label));
  row = g_object_new (GTK_TYPE_LIST_BOX_ROW,
                      "child", box,
                      NULL);
  g_object_set_data_full (G_OBJECT (row),
                          "GREETER_PAGE",
                          g_object_ref (page),
                          g_object_unref);
  return row;
}

static void
delete_envvar_cb (SysprofGreeter *self,
                  GtkButton      *button)
{
  const char *envvar;
  guint n_items;

  g_assert (SYSPROF_IS_GREETER (self));
  g_assert (GTK_IS_BUTTON (button));

  envvar = g_object_get_data (G_OBJECT (button), "ENVVAR");
  n_items = g_list_model_get_n_items (G_LIST_MODEL (self->envvars));

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(GtkStringObject) str = g_list_model_get_item (G_LIST_MODEL (self->envvars), i);

      if (g_strcmp0 (envvar, gtk_string_object_get_string (str)) == 0)
        {
          gtk_string_list_remove (self->envvars, i);
          break;
        }
    }
}

static GtkWidget *
create_envvar_row_cb (gpointer item,
                      gpointer user_data)
{
  SysprofGreeter *self = user_data;
  GtkStringObject *obj = item;
  const char *str;
  g_autofree char *markup = NULL;
  g_autofree char *escaped = NULL;
  AdwActionRow *row;
  GtkButton *button;

  g_assert (SYSPROF_IS_GREETER (self));
  g_assert (GTK_IS_STRING_OBJECT (obj));

  str = gtk_string_object_get_string (obj);
  escaped = g_markup_escape_text (str, -1);
  markup = g_strdup_printf ("<tt>%s</tt>", escaped);
  row = g_object_new (ADW_TYPE_ACTION_ROW,
                      "title", markup,
                      "title-selectable", TRUE,
                      NULL);
  button = g_object_new (GTK_TYPE_BUTTON,
                         "icon-name", "list-remove-symbolic",
                         "css-classes", STRV_INIT ("flat", "circular"),
                         "valign", GTK_ALIGN_CENTER,
                         NULL);
  g_object_set_data_full (G_OBJECT (button),
                          "ENVVAR",
                          g_strdup (str),
                          g_free);
  g_signal_connect_object (button,
                           "clicked",
                           G_CALLBACK (delete_envvar_cb),
                           self,
                           G_CONNECT_SWAPPED);
  adw_action_row_add_suffix (row, GTK_WIDGET (button));

  return GTK_WIDGET (row);
}

static char *
translate_power_profile (GtkStringObject *strobj)
{
  const char *str = gtk_string_object_get_string (strobj);

  if (g_str_equal (str, ""))
    return g_strdup (_("No Change"));

  if (g_str_equal (str, "balanced"))
    return g_strdup (_("Balanced"));

  if (g_str_equal (str, "power-saver"))
    return g_strdup (_("Power Saver"));

  if (g_str_equal (str, "performance"))
    return g_strdup (_("Performance"));

  return g_strdup (str);
}

static void
sysprof_greeter_dispose (GObject *object)
{
  SysprofGreeter *self = (SysprofGreeter *)object;

  gtk_widget_dispose_template (GTK_WIDGET (self), SYSPROF_TYPE_GREETER);

  g_clear_object (&self->envvars);

  G_OBJECT_CLASS (sysprof_greeter_parent_class)->dispose (object);
}

static void
sysprof_greeter_class_init (SysprofGreeterClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_greeter_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-greeter.ui");

  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, app_environment);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, bundle_symbols);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, envvars);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, power_combo);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, record_compositor);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, record_disk_usage);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, record_network_usage);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, record_page);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, record_session_bus);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, record_system_bus);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, record_system_logs);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, record_to_memory);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, recording_template);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, sample_javascript_stacks);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, sample_native_stacks);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, sidebar_list_box);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, view_stack);

  gtk_widget_class_bind_template_callback (widget_class, sidebar_row_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, get_file_path);
  gtk_widget_class_bind_template_callback (widget_class, on_env_entry_activate_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_env_entry_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, translate_power_profile);

  gtk_widget_class_install_action (widget_class, "win.record-to-memory", NULL, sysprof_greeter_record_to_memory_action);
  gtk_widget_class_install_action (widget_class, "win.record-to-file", NULL, sysprof_greeter_record_to_file_action);
  gtk_widget_class_install_action (widget_class, "win.select-file", NULL, sysprof_greeter_select_file_action);

  g_type_ensure (SYSPROF_TYPE_ENTRY_POPOVER);
  g_type_ensure (SYSPROF_TYPE_RECORDING_TEMPLATE);
}

static void
sysprof_greeter_init (SysprofGreeter *self)
{
  g_autoptr(GListModel) power_profiles = sysprof_power_profiles_new ();
  GtkListBoxRow *row;

  gtk_widget_init_template (GTK_WIDGET (self));

  adw_combo_row_set_model (self->power_combo, power_profiles);

  g_signal_connect_object (self->envvars,
                           "items-changed",
                           G_CALLBACK (on_env_items_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);
  on_env_items_changed_cb (self, 0, 0, 0, G_LIST_MODEL (self->envvars));

  gtk_list_box_bind_model (self->sidebar_list_box,
                           G_LIST_MODEL (adw_view_stack_get_pages (self->view_stack)),
                           sysprof_greeter_create_sidebar_row,
                           NULL, NULL);

  gtk_list_box_bind_model (self->app_environment,
                           G_LIST_MODEL (self->envvars),
                           create_envvar_row_cb,
                           self, NULL);

  row = gtk_list_box_get_row_at_index (self->sidebar_list_box, 0);
  gtk_list_box_select_row (self->sidebar_list_box, row);
  sidebar_row_activated_cb (self, row, self->sidebar_list_box);

  gtk_widget_grab_focus (GTK_WIDGET (self->record_to_memory));
}

GtkWidget *
sysprof_greeter_new (void)
{
  return g_object_new (SYSPROF_TYPE_GREETER, NULL);
}
