#include <sysprof.h>
#include <sysprof-ui.h>
#include <string.h>

static GtkWidget *list;

static GtkWidget *
create_row (gpointer item,
            gpointer user_data)
{
  return sysprof_process_model_row_new (item);
}

static gboolean
filter_cb (GObject  *object,
           gpointer  user_data)
{
  const gchar *needle = user_data;
  const gchar *command = sysprof_process_model_item_get_command_line (SYSPROF_PROCESS_MODEL_ITEM (object));

  return !!strstr (command, needle);
}

static void
on_entry_changed (GtkEntry      *entry,
                  SysprofModelFilter *filter)
{
  const gchar *text;

  g_assert (GTK_IS_ENTRY (entry));
  g_assert (SYSPROF_IS_MODEL_FILTER (filter));

  text = gtk_entry_get_text (entry);
  sysprof_model_filter_set_filter_func (filter, filter_cb, g_strdup (text), g_free);

  //gtk_list_box_bind_model (GTK_LIST_BOX (list), G_LIST_MODEL (filter), create_row, NULL, NULL);
}

gint
main (gint argc,
      gchar *argv[])
{
  SysprofProcessModel *model;
  SysprofModelFilter *filter;
  GtkWidget *window;
  GtkWidget *box;
  GtkWidget *scroller;
  GtkWidget *entry;

  gtk_init (&argc, &argv);

  window = g_object_new (GTK_TYPE_WINDOW,
                         "title", "Sysprof Process List",
                         "default-height", 700,
                         "default-width", 300,
                         NULL);

  box = g_object_new (GTK_TYPE_BOX,
                      "orientation", GTK_ORIENTATION_VERTICAL,
                      "visible", TRUE,
                      NULL);
  gtk_container_add (GTK_CONTAINER (window), box);

  entry = g_object_new (GTK_TYPE_ENTRY,
                        "visible", TRUE,
                        NULL);
  gtk_container_add (GTK_CONTAINER (box), entry);

  scroller = g_object_new (GTK_TYPE_SCROLLED_WINDOW,
                           "visible", TRUE,
                           "expand", TRUE,
                           NULL);
  gtk_container_add (GTK_CONTAINER (box), scroller);

  list = g_object_new (GTK_TYPE_LIST_BOX,
                       "visible", TRUE,
                       NULL);
  gtk_container_add (GTK_CONTAINER (scroller), list);

  model = sysprof_process_model_new ();
  sysprof_process_model_set_no_proxy (model, TRUE);
  filter = sysprof_model_filter_new (G_LIST_MODEL (model));
  gtk_list_box_bind_model (GTK_LIST_BOX (list), G_LIST_MODEL (filter), create_row, NULL, NULL);

  g_signal_connect (entry,
                    "changed",
                    G_CALLBACK (on_entry_changed),
                    filter);

  gtk_window_present (GTK_WINDOW (window));
  g_signal_connect (window, "delete-event", gtk_main_quit, NULL);
  gtk_main ();

  g_object_unref (model);

  return 0;
}
