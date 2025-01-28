/* sysprof-recording-pad.c
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

#include "sysprof-application.h"
#include "sysprof-recording-pad.h"
#include "sysprof-window.h"

struct _SysprofRecordingPad
{
  AdwWindow                 parent_instance;

  SysprofRecording         *recording;
  SysprofRecordingTemplate *template;

  GtkButton                *stop_button;

  guint                     closed : 1;
};

enum {
  PROP_0,
  PROP_RECORDING,
  PROP_TEMPLATE,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofRecordingPad, sysprof_recording_pad, ADW_TYPE_WINDOW)

static GParamSpec *properties[N_PROPS];

static char *
format_event_count (GObject *unused,
                    gint64   event_count)
{
  /* translators: this expands to the number of events recorded by the profiler as an indicator of progress */
  return g_strdup_printf (_("%"PRIi64" events"), event_count);
}

static char *
format_time (GObject *unused,
             gint64   duration)
{
  double elapsed = duration / (double)G_USEC_PER_SEC;
  int minutes = floor (elapsed / 60);
  int seconds = floor (elapsed - (minutes * 60));

  return g_strdup_printf ("%02u:%02u", minutes, seconds);
}

static gboolean
sysprof_recording_pad_close_request (GtkWindow *window)
{
  SysprofRecordingPad *self = (SysprofRecordingPad *)window;

  g_assert (SYSPROF_IS_RECORDING_PAD (self));
  g_assert (self->recording != NULL);

  if (!self->closed)
    {
      g_debug ("User requested to stop recording");

      self->closed = TRUE;
      sysprof_recording_stop_async (self->recording, NULL, NULL, NULL);
    }

  return GDK_EVENT_PROPAGATE;
}

static void
sysprof_recording_pad_wait_cb (GObject      *object,
                               GAsyncResult *result,
                               gpointer      user_data)
{
  SysprofRecording *recording = (SysprofRecording *)object;
  g_autoptr(SysprofRecordingPad) self = user_data;
  g_autoptr(GError) error = NULL;
  g_autofd int fd = -1;

  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (SYSPROF_IS_RECORDING_PAD (self));

  g_debug ("Recording has indicated completion");

  g_application_release (G_APPLICATION (SYSPROF_APPLICATION_DEFAULT));

  if (!sysprof_recording_wait_finish (recording, result, &error))
    {
      GtkWidget *dialog;

      G_GNUC_BEGIN_IGNORE_DEPRECATIONS

      dialog = adw_message_dialog_new (NULL, _("Recording Failed"), NULL);
      adw_message_dialog_format_body (ADW_MESSAGE_DIALOG (dialog),
                                      _("Sysprof failed to record.\n\n%s"),
                                      error->message);
      adw_message_dialog_add_response (ADW_MESSAGE_DIALOG (dialog), "close", _("Close"));
      gtk_application_add_window (GTK_APPLICATION (SYSPROF_APPLICATION_DEFAULT),
                                  GTK_WINDOW (dialog));
      gtk_window_present (GTK_WINDOW (dialog));

      G_GNUC_END_IGNORE_DEPRECATIONS
    }
  else if (-1 != (fd = sysprof_recording_dup_fd (self->recording)))
    {
      lseek (fd, 0, SEEK_SET);
      sysprof_window_open_fd (SYSPROF_APPLICATION_DEFAULT, self->template, fd);
    }

  if (!self->closed)
    gtk_window_destroy (GTK_WINDOW (self));
}

static void
sysprof_recording_pad_constructed (GObject *object)
{
  SysprofRecordingPad *self = (SysprofRecordingPad *)object;

  G_OBJECT_CLASS (sysprof_recording_pad_parent_class)->constructed (object);

  g_application_hold (G_APPLICATION (SYSPROF_APPLICATION_DEFAULT));

  g_debug ("Waiting for completion of recording");

  sysprof_recording_wait_async (self->recording,
                                NULL,
                                sysprof_recording_pad_wait_cb,
                                g_object_ref (self));
}

static void
sysprof_recording_pad_dispose (GObject *object)
{
  SysprofRecordingPad *self = (SysprofRecordingPad *)object;

  g_clear_object (&self->recording);
  g_clear_object (&self->template);

  G_OBJECT_CLASS (sysprof_recording_pad_parent_class)->dispose (object);
}

static void
sysprof_recording_pad_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  SysprofRecordingPad *self = SYSPROF_RECORDING_PAD (object);

  switch (prop_id)
    {
    case PROP_RECORDING:
      g_value_set_object (value, self->recording);
      break;

    case PROP_TEMPLATE:
      g_value_set_object (value, self->template);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_recording_pad_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  SysprofRecordingPad *self = SYSPROF_RECORDING_PAD (object);

  switch (prop_id)
    {
    case PROP_RECORDING:
      self->recording = g_value_dup_object (value);
      break;

    case PROP_TEMPLATE:
      self->template = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_recording_pad_class_init (SysprofRecordingPadClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkWindowClass *window_class = GTK_WINDOW_CLASS (klass);

  object_class->constructed = sysprof_recording_pad_constructed;
  object_class->dispose = sysprof_recording_pad_dispose;
  object_class->get_property = sysprof_recording_pad_get_property;
  object_class->set_property = sysprof_recording_pad_set_property;

  window_class->close_request = sysprof_recording_pad_close_request;

  properties [PROP_RECORDING] =
    g_param_spec_object ("recording", NULL, NULL,
                         SYSPROF_TYPE_RECORDING,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_TEMPLATE] =
    g_param_spec_object ("template", NULL, NULL,
                         SYSPROF_TYPE_RECORDING_TEMPLATE,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-recording-pad.ui");
  gtk_widget_class_bind_template_child (widget_class, SysprofRecordingPad, stop_button);
  gtk_widget_class_bind_template_callback (widget_class, format_time);
  gtk_widget_class_bind_template_callback (widget_class, format_event_count);
}

static void
sysprof_recording_pad_init (SysprofRecordingPad *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_widget_grab_focus (GTK_WIDGET (self->stop_button));
}

GtkWidget *
sysprof_recording_pad_new (SysprofRecording         *recording,
                           SysprofRecordingTemplate *template)
{
  g_return_val_if_fail (SYSPROF_IS_RECORDING (recording), NULL);
  g_return_val_if_fail (!template || SYSPROF_IS_RECORDING_TEMPLATE (template), NULL);

  return g_object_new (SYSPROF_TYPE_RECORDING_PAD,
                       "application", SYSPROF_APPLICATION_DEFAULT,
                       "recording", recording,
                       "template", template,
                       NULL);
}
