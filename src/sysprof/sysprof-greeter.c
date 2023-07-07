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

#include <glib/gstdio.h>

#include <sysprof-capture.h>
#include <sysprof-profiler.h>

#include "sysprof-greeter.h"
#include "sysprof-recording-pad.h"

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureWriter, sysprof_capture_writer_unref)

struct _SysprofGreeter
{
  AdwWindow  parent_instance;

  GtkBox             *toolbar;
  AdwPreferencesPage *record_page;
  GtkSwitch          *sample_native_stacks;
  GtkSwitch          *record_disk_usage;
  GtkSwitch          *record_network_usage;
};

enum {
  PROP_0,
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

  g_assert (SYSPROF_IS_GREETER (self));

  profiler = sysprof_profiler_new ();

  if (gtk_switch_get_active (self->sample_native_stacks))
    sysprof_profiler_add_instrument (profiler, sysprof_sampler_new ());

  if (gtk_switch_get_active (self->record_disk_usage))
    sysprof_profiler_add_instrument (profiler, sysprof_disk_usage_new ());

  if (gtk_switch_get_active (self->record_network_usage))
    sysprof_profiler_add_instrument (profiler, sysprof_network_usage_new ());

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

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-greeter.ui");
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, toolbar);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, record_page);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, record_disk_usage);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, record_network_usage);
  gtk_widget_class_bind_template_child (widget_class, SysprofGreeter, sample_native_stacks);
  gtk_widget_class_bind_template_callback (widget_class, sysprof_greeter_view_stack_notify_visible_child);

  gtk_widget_class_install_action (widget_class, "win.record-to-memory", NULL, sysprof_greeter_record_to_memory_action);
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
