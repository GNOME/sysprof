/* test-processes.c
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

#include "tests-resources.h"

#include <adwaita.h>
#include <libpanel.h>
#include <gtk/gtk.h>

#include <sysprof-analyze.h>
#include <sysprof-gtk.h>

static GMainLoop *main_loop;
static char *filename;
static const GOptionEntry entries[] = {
  { 0 }
};

#define TEST_TYPE_SINGLE_MODEL (test_single_model_get_type())
G_DECLARE_FINAL_TYPE (TestSingleModel, test_single_model, TEST, SINGLE_MODEL, GObject)

struct _TestSingleModel
{
  GObject parent_instance;
  GObject *item;
};

enum {
  MODEL_PROP_0,
  MODEL_PROP_ITEM,
  MODEL_N_PROPS
};

static GParamSpec *model_properties[MODEL_N_PROPS];

static guint
get_n_items (GListModel *model)
{
  return TEST_SINGLE_MODEL (model)->item ? 1 : 0;
}

static gpointer
get_item (GListModel *model,
          guint       position)
{
  if (position == 0 && TEST_SINGLE_MODEL (model)->item)
    return g_object_ref (TEST_SINGLE_MODEL (model)->item);
  return NULL;
}

static GType
get_item_type (GListModel *model)
{
  if (TEST_SINGLE_MODEL (model)->item)
    return G_OBJECT_TYPE (TEST_SINGLE_MODEL (model)->item);
  return G_TYPE_OBJECT;
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_n_items = get_n_items;
  iface->get_item = get_item;
  iface->get_item_type = get_item_type;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (TestSingleModel, test_single_model, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

static void
test_single_model_finalize (GObject *object)
{
  TestSingleModel *self = (TestSingleModel *)object;

  g_clear_object (&self->item);

  G_OBJECT_CLASS (test_single_model_parent_class)->finalize (object);
}

static void
test_single_model_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  TestSingleModel *self = TEST_SINGLE_MODEL (object);

  switch (prop_id)
    {
    case MODEL_PROP_ITEM:
      g_value_set_object (value, self->item);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
test_single_model_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  TestSingleModel *self = TEST_SINGLE_MODEL (object);

  switch (prop_id)
    {
    case MODEL_PROP_ITEM:
      if (self->item != NULL)
        {
          g_clear_object (&self->item);
          g_list_model_items_changed (G_LIST_MODEL (self), 0, 1, 0);
        }
      self->item = g_value_dup_object (value);
      if (self->item != NULL)
        g_list_model_items_changed (G_LIST_MODEL (self), 0, 0, 1);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
test_single_model_class_init (TestSingleModelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = test_single_model_finalize;
  object_class->get_property = test_single_model_get_property;
  object_class->set_property = test_single_model_set_property;

  model_properties[MODEL_PROP_ITEM] =
    g_param_spec_object ("item", NULL, NULL,
                         G_TYPE_OBJECT,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, MODEL_N_PROPS, model_properties);
}

static void
test_single_model_init (TestSingleModel *self)
{
}

#define TEST_TYPE_PROCESSES (test_processes_get_type())
G_DECLARE_FINAL_TYPE (TestProcesses, test_processes, TEST, PROCESSES, AdwWindow)

struct _TestProcesses
{
  AdwWindow        parent_instance;

  SysprofDocument *document;
  SysprofSession  *session;
};

G_DEFINE_FINAL_TYPE (TestProcesses, test_processes, ADW_TYPE_WINDOW)

enum {
  PROP_0,
  PROP_DOCUMENT,
  PROP_SESSION,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

static void
test_processes_set_document (TestProcesses   *self,
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
test_processes_dispose (GObject *object)
{
  TestProcesses *self = (TestProcesses *)object;

  g_clear_object (&self->document);
  g_clear_object (&self->session);

  G_OBJECT_CLASS (test_processes_parent_class)->dispose (object);
}

static void
test_processes_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  TestProcesses *self = TEST_PROCESSES (object);

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
test_processes_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  TestProcesses *self = TEST_PROCESSES (object);

  switch (prop_id)
    {
    case PROP_DOCUMENT:
      test_processes_set_document (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
test_processes_class_init (TestProcessesClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = test_processes_dispose;
  object_class->get_property = test_processes_get_property;
  object_class->set_property = test_processes_set_property;

  properties [PROP_DOCUMENT] =
    g_param_spec_object ("document", NULL, NULL,
                         SYSPROF_TYPE_DOCUMENT,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SESSION] =
    g_param_spec_object ("session", NULL, NULL,
                         SYSPROF_TYPE_SESSION,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/test-processes.ui");

  g_type_ensure (TEST_TYPE_SINGLE_MODEL);
  g_type_ensure (SYSPROF_TYPE_CHART);
  g_type_ensure (SYSPROF_TYPE_CHART_LAYER);
  g_type_ensure (SYSPROF_TYPE_COLUMN_LAYER);
  g_type_ensure (SYSPROF_TYPE_LINE_LAYER);
  g_type_ensure (SYSPROF_TYPE_VALUE_AXIS);
  g_type_ensure (SYSPROF_TYPE_SESSION_MODEL);
  g_type_ensure (SYSPROF_TYPE_SESSION_MODEL_ITEM);
  g_type_ensure (SYSPROF_TYPE_TIME_RULER);
  g_type_ensure (SYSPROF_TYPE_TIME_SPAN_LAYER);
  g_type_ensure (SYSPROF_TYPE_TIME_SERIES);
}

static void
test_processes_init (TestProcesses *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr(GOptionContext) context = g_option_context_new ("- test processes listing");
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

  g_resources_register (tests_get_resource ());

  filename = argv[1];

  main_loop = g_main_loop_new (NULL, FALSE);

  loader = sysprof_document_loader_new (filename);
  if (!(document = sysprof_document_loader_load (loader, NULL, &error)))
    g_error ("Failed to load document: %s", error->message);

  window = g_object_new (TEST_TYPE_PROCESSES,
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
