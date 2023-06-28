/* test-charts.c
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

#include <sysprof-analyze.h>
#include <sysprof-gtk.h>

static GMainLoop *main_loop;
static char *filename;
static const GOptionEntry entries[] = {
  { 0 }
};

#define TEST_TYPE_CHARTS (test_charts_get_type())
G_DECLARE_FINAL_TYPE (TestCharts, test_charts, TEST, CHARTS, AdwWindow)

struct _TestCharts
{
  AdwWindow        parent_instance;

  SysprofDocument *document;
  SysprofSession  *session;
};

G_DEFINE_FINAL_TYPE (TestCharts, test_charts, ADW_TYPE_WINDOW)

enum {
  PROP_0,
  PROP_DOCUMENT,
  PROP_SESSION,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

static gboolean
activate_layer_item_cb (SysprofChart      *chart,
                        SysprofChartLayer *layer,
                        gpointer           item,
                        TestCharts        *test)
{
  g_assert (SYSPROF_IS_CHART (chart));
  g_assert (SYSPROF_IS_CHART_LAYER (layer));
  g_assert (G_IS_OBJECT (item));
  g_assert (TEST_IS_CHARTS (test));

  g_print ("Activated %s in layer '%s' [%s]\n",
           G_OBJECT_TYPE_NAME (item),
           sysprof_chart_layer_get_title (layer),
           G_OBJECT_TYPE_NAME (layer));

  if (SYSPROF_IS_DOCUMENT_FRAME (item))
    {
      g_print ("time_offset=%"G_GINT64_FORMAT" pid=%d\n",
               sysprof_document_frame_get_time_offset (item),
               sysprof_document_frame_get_pid (item));
    }

  if (SYSPROF_IS_DOCUMENT_TRACEABLE (item))
    {
      guint depth = sysprof_document_traceable_get_stack_depth (item);

      g_print ("Thread-Id: %u\n",
               sysprof_document_traceable_get_thread_id (item));
      g_print ("Stack Depth: %u\n", depth);

      if (depth <= 128)
        {
          SysprofSymbol *symbols[128];
          SysprofAddressContext final_context;
          guint n_symbols = G_N_ELEMENTS (symbols);

          n_symbols = sysprof_document_symbolize_traceable (test->document,
                                                            item,
                                                            symbols,
                                                            n_symbols,
                                                            &final_context);

          for (guint i = 0; i < n_symbols; i++)
            g_print ("  %s\n", sysprof_symbol_get_name (symbols[i]));
        }
    }

  return GDK_EVENT_STOP;
}

static void
test_charts_set_document (TestCharts      *self,
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
test_charts_dispose (GObject *object)
{
  TestCharts *self = (TestCharts *)object;

  g_clear_object (&self->document);
  g_clear_object (&self->session);

  G_OBJECT_CLASS (test_charts_parent_class)->dispose (object);
}

static void
test_charts_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  TestCharts *self = TEST_CHARTS (object);

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
test_charts_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  TestCharts *self = TEST_CHARTS (object);

  switch (prop_id)
    {
    case PROP_DOCUMENT:
      test_charts_set_document (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
test_charts_class_init (TestChartsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = test_charts_dispose;
  object_class->get_property = test_charts_get_property;
  object_class->set_property = test_charts_set_property;

  properties [PROP_DOCUMENT] =
    g_param_spec_object ("document", NULL, NULL,
                         SYSPROF_TYPE_DOCUMENT,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SESSION] =
    g_param_spec_object ("session", NULL, NULL,
                         SYSPROF_TYPE_SESSION,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/test-charts.ui");
  gtk_widget_class_bind_template_callback (widget_class, activate_layer_item_cb);

  g_type_ensure (SYSPROF_TYPE_CHART);
  g_type_ensure (SYSPROF_TYPE_CHART_LAYER);
  g_type_ensure (SYSPROF_TYPE_COLUMN_LAYER);
  g_type_ensure (SYSPROF_TYPE_LINE_LAYER);
  g_type_ensure (SYSPROF_TYPE_DOCUMENT_COUNTER_VALUE);
  g_type_ensure (SYSPROF_TYPE_VALUE_AXIS);
  g_type_ensure (SYSPROF_TYPE_SESSION_MODEL);
  g_type_ensure (SYSPROF_TYPE_SESSION_MODEL_ITEM);
  g_type_ensure (SYSPROF_TYPE_DUPLEX_LAYER);
  g_type_ensure (SYSPROF_TYPE_TIME_RULER);
}

static void
test_charts_init (TestCharts *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr(GOptionContext) context = g_option_context_new ("- test various charts");
  g_autoptr(SysprofDocumentLoader) loader = NULL;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(GError) error = NULL;
  GtkWindow *window;

  sysprof_clock_init ();

  gtk_init ();
  adw_init ();

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

  window = g_object_new (TEST_TYPE_CHARTS,
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
