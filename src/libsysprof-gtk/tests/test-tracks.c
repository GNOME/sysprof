/* test-tracks.c
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

#include <adwaita.h>
#include <gtk/gtk.h>
#include <libpanel.h>

#include <sysprof-analyze.h>
#include <sysprof-gtk.h>

static GMainLoop *main_loop;
static char *filename;
static const GOptionEntry entries[] = {
  { 0 }
};

#define TEST_TYPE_TRACKS (test_tracks_get_type())
G_DECLARE_FINAL_TYPE (TestTracks, test_tracks, TEST, TRACKS, AdwWindow)

struct _TestTracks
{
  AdwWindow        parent_instance;

  SysprofDocument *document;
  SysprofSession  *session;
};

G_DEFINE_FINAL_TYPE (TestTracks, test_tracks, ADW_TYPE_WINDOW)

enum {
  PROP_0,
  PROP_DOCUMENT,
  PROP_SESSION,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

static void
test_tracks_set_document (TestTracks      *self,
                          SysprofDocument *document)
{
  if (g_set_object (&self->document, document))
    {
      g_clear_object (&self->session);

      self->session = sysprof_session_new (self->document);

      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DOCUMENT]);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SESSION]);
    }
}

static void
test_tracks_dispose (GObject *object)
{
  TestTracks *self = (TestTracks *)object;

  g_clear_object (&self->document);
  g_clear_object (&self->session);

  G_OBJECT_CLASS (test_tracks_parent_class)->dispose (object);
}

static void
test_tracks_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  TestTracks *self = TEST_TRACKS (object);

  switch (prop_id)
    {
    case PROP_DOCUMENT:
      g_value_set_object (value, self->document);
      break;

    case PROP_SESSION:
      g_value_set_object (value, self->session);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
test_tracks_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  TestTracks *self = TEST_TRACKS (object);

  switch (prop_id)
    {
    case PROP_DOCUMENT:
      test_tracks_set_document (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
test_tracks_class_init (TestTracksClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = test_tracks_dispose;
  object_class->get_property = test_tracks_get_property;
  object_class->set_property = test_tracks_set_property;

  properties [PROP_DOCUMENT] =
    g_param_spec_object ("document", NULL, NULL,
                         SYSPROF_TYPE_DOCUMENT,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SESSION] =
    g_param_spec_object ("session", NULL, NULL,
                         SYSPROF_TYPE_SESSION,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/test-tracks.ui");

  g_type_ensure (SYSPROF_TYPE_TRACKS_VIEW);
  g_type_ensure (SYSPROF_TYPE_MARK_CHART);
  g_type_ensure (SYSPROF_TYPE_MARK_TABLE);
  g_type_ensure (SYSPROF_TYPE_WEIGHTED_CALLGRAPH_VIEW);
}

static void
test_tracks_init (TestTracks *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr(GOptionContext) context = g_option_context_new ("- test track layout");
  g_autoptr(SysprofDocumentLoader) loader = NULL;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(GError) error = NULL;
  GtkWindow *window;

  sysprof_clock_init ();

  gtk_init ();
  adw_init ();
  panel_init ();

  g_option_context_add_main_entries (context, entries, NULL);
  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("%s\n", error->message);
      return 1;
    }

  if (argc != 2)
    {
      g_print ("usage: %s [OPTIONS] CAPTURE_FILE\n", argv[0]);
      return 1;
    }

  filename = argv[1];

  main_loop = g_main_loop_new (NULL, FALSE);

  loader = sysprof_document_loader_new (filename);
  if (!(document = sysprof_document_loader_load (loader, NULL, &error)))
    g_error ("Failed to load document: %s", error->message);

  window = g_object_new (TEST_TYPE_TRACKS,
                         "default-width", 800,
                         "default-height", 600,
                         "document", document,
                         NULL);
  g_signal_connect_swapped (window,
                            "close-request",
                            G_CALLBACK (g_main_loop_quit),
                            main_loop);
  gtk_window_present (window);
  g_main_loop_run (main_loop);

  return 0;
}
