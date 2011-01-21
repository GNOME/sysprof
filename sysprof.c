/* Sysprof -- Sampling, systemwide CPU profiler
 * Copyright 2004, Red Hat, Inc.
 * Copyright 2004, 2005, 2006, 2007, 2008, Soeren Sandmann
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include <config.h>

#include <gtk/gtk.h>
#include <glade/glade.h>
#include <errno.h>
#include <glib/gprintf.h>
#include <string.h>
#include <stdlib.h>

#include "footreestore.h"
#include "treeviewutils.h"
#include "profile.h"
#include "collector.h"

/* FIXME - not10 */
#define _(a) a

#define APPLICATION_NAME "System Profiler"

typedef struct Application Application;

typedef enum
{
    INITIAL,
    DISPLAYING,
    PROFILING
} State;

struct Application
{
    Collector *		collector;

    State		state;
    GdkPixbuf *		icon;

    GtkWidget *		main_window;

    GtkTreeView *	object_view;
    GtkTreeView *	callers_view;
    GtkTreeView *	descendants_view;

    GtkWidget *		start_button;
    GtkWidget *		profile_button;
    GtkWidget *		reset_button;
    GtkWidget *		save_as_button;
    GtkWidget *		dummy_button;

    GtkWidget *		start_item;
    GtkWidget *		profile_item;
    GtkWidget *		reset_item;
    GtkWidget *		save_as_item;
    GtkWidget *		open_item;
    GtkWidget *		screenshot_item;
    GtkWidget *		about_item;
    GtkWidget *		quit_item;
    GtkWidget *		hpaned;
    GtkWidget *		vpaned;

    GtkTreeSelection *	object_selection;

    GtkWidget *		samples_label;
    GtkWidget *		samples_hbox;

    gboolean		screenshot_window_visible;
    GtkWidget *		screenshot_textview;
    GtkWidget *		screenshot_close_button;
    GtkWidget *         screenshot_window;

    Profile *		profile;
    ProfileDescendant * descendants;
    ProfileCaller *	callers;

    int			timeout_id;
    int			update_screenshot_id;

    char *		loaded_profile;

    gboolean		inhibit_forced_redraw;
};

static void update_screenshot_window (Application *app);

static void
show_samples (Application *app)
{
    char *label;
    int n_samples;

    switch (app->state)
    {
    case INITIAL:
	n_samples = 0;
	break;

    case PROFILING:
	n_samples = collector_get_n_samples (app->collector);
	break;

    case DISPLAYING:
	n_samples = profile_get_size (app->profile);
	break;

    default:
	g_assert_not_reached();
	break;
    }

    label = g_strdup_printf ("%d", n_samples);

    gtk_label_set_label (GTK_LABEL (app->samples_label), label);

    g_free (label);
}

static gboolean
show_samples_timeout (gpointer data)
{
    Application *app = data;

    show_samples (app);

    app->timeout_id = 0;

    return FALSE;
}

static void
queue_show_samples (Application *app)
{
    if (!app->timeout_id)
	app->timeout_id = g_timeout_add (225, show_samples_timeout, app);
}

static void
update_sensitivity (Application *app)
{
    gboolean sensitive_profile_button;
    gboolean sensitive_save_as_button;
    gboolean sensitive_start_button;
    gboolean sensitive_tree_views;
    gboolean sensitive_samples_hbox;
    gboolean sensitive_reset_button;

    GtkWidget *active_radio_button;

    gboolean has_samples;

    switch (app->state)
    {
    case INITIAL:
	sensitive_profile_button = FALSE;
	sensitive_save_as_button = FALSE;
	sensitive_start_button = TRUE;
	sensitive_reset_button = FALSE;
	sensitive_tree_views = FALSE;
	sensitive_samples_hbox = FALSE;
	active_radio_button = app->dummy_button;
	break;

    case PROFILING:
	has_samples = (collector_get_n_samples (app->collector) > 0);

	sensitive_profile_button = has_samples;
	sensitive_save_as_button = has_samples;
	sensitive_reset_button = has_samples;
	sensitive_start_button = TRUE;
	sensitive_tree_views = FALSE;
	sensitive_samples_hbox = TRUE;
	active_radio_button = app->start_button;
	break;

    case DISPLAYING:
	sensitive_profile_button = TRUE;
	sensitive_save_as_button = TRUE;
	sensitive_start_button = TRUE;
	sensitive_tree_views = TRUE;
	sensitive_reset_button = TRUE;
	sensitive_samples_hbox = FALSE;
	active_radio_button = app->profile_button;
	break;

    default:
	g_assert_not_reached();
	break;
    }

    gtk_toggle_tool_button_set_active (
	GTK_TOGGLE_TOOL_BUTTON (active_radio_button), TRUE);

    /* "profile" widgets */
    gtk_widget_set_sensitive (GTK_WIDGET (app->profile_button),
			      sensitive_profile_button);
    gtk_widget_set_sensitive (GTK_WIDGET (app->profile_item),
			      sensitive_profile_button);

    /* "save as" widgets */
    gtk_widget_set_sensitive (GTK_WIDGET (app->save_as_button),
			      sensitive_save_as_button);
    gtk_widget_set_sensitive (app->save_as_item,
			      sensitive_save_as_button);

    /* "start" widgets */
    gtk_widget_set_sensitive (GTK_WIDGET (app->start_button),
			      sensitive_start_button);
    gtk_widget_set_sensitive (GTK_WIDGET (app->start_item),
			      sensitive_start_button);

#if 0
    /* FIXME - not10: gtk+ doesn't handle changes in sensitivity in response
     * to a click on the same button very well
     */
    gtk_widget_set_sensitive (GTK_WIDGET (app->reset_button),
			      sensitive_reset_button);
    gtk_widget_set_sensitive (GTK_WIDGET (app->reset_item),
			      sensitive_reset_button);
#endif

    gtk_widget_set_sensitive (GTK_WIDGET (app->object_view), sensitive_tree_views);
    gtk_widget_set_sensitive (GTK_WIDGET (app->callers_view), sensitive_tree_views);
    gtk_widget_set_sensitive (GTK_WIDGET (app->descendants_view), sensitive_tree_views);
    gtk_widget_set_sensitive (GTK_WIDGET (app->samples_hbox), sensitive_samples_hbox);

    if (app->screenshot_window_visible)
	gtk_widget_show (app->screenshot_window);
    else
	gtk_widget_hide (app->screenshot_window);

    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (app->screenshot_item),
				    app->screenshot_window_visible);

    show_samples (app);
}

static void
set_busy (GtkWidget *widget,
	  gboolean   busy)
{
    GdkCursor *cursor;
    GdkWindow *window;

    if (busy)
	cursor = gdk_cursor_new (GDK_WATCH);
    else
	cursor = NULL;

    if (GTK_IS_TEXT_VIEW (widget))
	window = gtk_text_view_get_window (GTK_TEXT_VIEW (widget), GTK_TEXT_WINDOW_TEXT);
    else
	window = widget->window;

    gdk_window_set_cursor (window, cursor);

    if (cursor)
	gdk_cursor_unref (cursor);

    gdk_flush();
}

static void
set_application_title (Application *app,
		       const char * name)
{
    char *new_name;
    if (name)
	new_name = g_path_get_basename (name);
    else
	new_name = NULL;

    if (app->loaded_profile)
	g_free (app->loaded_profile);

    app->loaded_profile = new_name;

    if (app->loaded_profile)
    {
	gtk_window_set_title (GTK_WINDOW (app->main_window),
			      app->loaded_profile);
    }
    else
    {
	gtk_window_set_title (GTK_WINDOW (app->main_window),
			      APPLICATION_NAME);
    }
}

static void
delete_data (Application *app)
{
    if (app->profile)
    {
	profile_free (app->profile);
	app->profile = NULL;

	gtk_tree_view_set_model (GTK_TREE_VIEW (app->object_view), NULL);
	gtk_tree_view_set_model (GTK_TREE_VIEW (app->callers_view), NULL);
	gtk_tree_view_set_model (GTK_TREE_VIEW (app->descendants_view), NULL);
    }

    collector_reset (app->collector);

    set_application_title (app, NULL);
}

static void
sorry (GtkWidget *parent_window,
       const gchar *format,
       ...)
{
    va_list args;
    char *message;
    GtkWidget *dialog;

    va_start (args, format);
    g_vasprintf (&message, format, args);
    va_end (args);

    dialog = gtk_message_dialog_new (parent_window ? GTK_WINDOW (parent_window) : NULL,
				     GTK_DIALOG_DESTROY_WITH_PARENT,
				     GTK_MESSAGE_WARNING,
				     GTK_BUTTONS_OK, "%s", message);
    g_free (message);

    gtk_window_set_title (GTK_WINDOW (dialog), APPLICATION_NAME " Warning");

    gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);
}

static void
on_menu_item_activated (GtkWidget *menu_item, GtkWidget *tool_button)
{
    GtkToggleToolButton *button = GTK_TOGGLE_TOOL_BUTTON (tool_button);

    if (!gtk_toggle_tool_button_get_active (button))
	gtk_toggle_tool_button_set_active (button, TRUE);
}

static void
on_start_toggled (GtkWidget *widget, gpointer data)
{
    Application *app = data;
    GError *err = NULL;

    if (!gtk_toggle_tool_button_get_active (
	    GTK_TOGGLE_TOOL_BUTTON (app->start_button)))
    {
	return;
    }

    if (collector_start (app->collector, -1, &err))
    {
	delete_data (app);

	app->state = PROFILING;
    }
    else
    {
	sorry (app->main_window, err->message); 

	g_error_free (err);
    }

    update_screenshot_window (app);
    update_sensitivity (app);
}

enum
{
    OBJECT_NAME,
    OBJECT_SELF,
    OBJECT_TOTAL,
    OBJECT_OBJECT
};

enum
{
    CALLERS_NAME,
    CALLERS_SELF,
    CALLERS_TOTAL,
    CALLERS_OBJECT
};

enum
{
    DESCENDANTS_NAME,
    DESCENDANTS_SELF,
    DESCENDANTS_CUMULATIVE,
    DESCENDANTS_OBJECT
};

static char *
get_current_object (Application *app)
{
    GtkTreeModel *model;
    GtkTreeIter selected;
    char *object;

    if (gtk_tree_selection_get_selected (app->object_selection, &model, &selected))
    {
	gtk_tree_model_get (model, &selected,
			    OBJECT_OBJECT, &object,
			    -1);
	return object;
    }
    else
    {
	return NULL;
    }
}

static void
fill_main_list (Application *app)
{
    GList *list;
    GtkListStore *list_store;
    Profile *profile = app->profile;
    GList *objects;

    if (profile)
    {
	list_store = gtk_list_store_new (4,
					 G_TYPE_STRING,
					 G_TYPE_DOUBLE,
					 G_TYPE_DOUBLE,
					 G_TYPE_POINTER);

	objects = profile_get_objects (profile);
	for (list = objects; list != NULL; list = list->next)
	{
	    ProfileObject *object = list->data;
	    GtkTreeIter iter;
	    double profile_size = profile_get_size (profile);

	    gtk_list_store_append (list_store, &iter);

	    gtk_list_store_set (list_store, &iter,
				OBJECT_NAME, object->name,
				OBJECT_SELF, 100.0 * object->self / profile_size,
				OBJECT_TOTAL, 100.0 * object->total / profile_size,
#if 0
				OBJECT_SELF, (double)object->self,
				OBJECT_TOTAL, (double)object->total,
#endif
				OBJECT_OBJECT, object->name,
				-1);
	}
	g_list_foreach (objects, (GFunc)g_free, NULL);
	g_list_free (objects);

	tree_view_set_model_with_default_sort (app->object_view, GTK_TREE_MODEL (list_store),
					       OBJECT_TOTAL, GTK_SORT_DESCENDING);

	g_object_unref (G_OBJECT (list_store));
    }

    gtk_tree_view_columns_autosize (app->object_view);
}

static void
add_node (FooTreeStore      *store,
	  int                size,
	  const GtkTreeIter *parent,
	  ProfileDescendant *node)
{
    GtkTreeIter iter;

    if (!node)
	return;
    
    foo_tree_store_insert (store, &iter, (GtkTreeIter *)parent, 0);
    
    foo_tree_store_set (store, &iter,
			DESCENDANTS_NAME, node->name,
			DESCENDANTS_SELF, 100 * (node->self)/(double)size,
			DESCENDANTS_CUMULATIVE, 100 * (node->cumulative)/(double)size,
#if 0
			DESCENDANTS_SELF, (double)node->self,
			DESCENDANTS_CUMULATIVE, (double)node->non_recursion,
#endif
			DESCENDANTS_OBJECT, node->name,
			-1);

    add_node (store, size, parent, node->siblings);
    add_node (store, size, &iter, node->children);
}

static void
fill_descendants_tree (Application *app)
{
    FooTreeStore *tree_store;

    if (app->descendants)
    {
	profile_descendant_free (app->descendants);
	app->descendants = NULL;
    }

    tree_store =
	foo_tree_store_new (4,
			    G_TYPE_STRING,
			    G_TYPE_DOUBLE,
			    G_TYPE_DOUBLE,
			    G_TYPE_POINTER);

    if (app->profile)
    {
	char *object = get_current_object (app);
	if (object)
	{
	    app->descendants =
		profile_create_descendants (app->profile, object);
	    add_node (tree_store,
		      profile_get_size (app->profile), NULL, app->descendants);
	}
    }

    tree_view_set_model_with_default_sort (app->descendants_view, GTK_TREE_MODEL (tree_store),
					   DESCENDANTS_CUMULATIVE, GTK_SORT_DESCENDING);

    g_object_unref (G_OBJECT (tree_store));

    gtk_tree_view_columns_autosize (app->descendants_view);
}

static void
add_callers (GtkListStore  *list_store,
	     Profile       *profile,
	     ProfileCaller *callers)
{
    while (callers)
    {
	gchar *name;
	GtkTreeIter iter;
	double profile_size = profile_get_size (profile);

	if (callers->name)
	    name = callers->name;
	else
	    name = "<spontaneous>";

	gtk_list_store_append (list_store, &iter);
	gtk_list_store_set (
	    list_store, &iter,
	    CALLERS_NAME, name,
	    CALLERS_SELF, 100.0 * callers->self / profile_size,
	    CALLERS_TOTAL, 100.0 * callers->total / profile_size,
#if 0
	    CALLERS_SELF, (double)callers->self,
	    CALLERS_TOTAL, (double)callers->total,
#endif
	    CALLERS_OBJECT, callers->name,
	    -1);

	callers = callers->next;
    }
}

static void
fill_callers_list (Application *app)
{
    GtkListStore *list_store;

    if (app->callers)
    {
	profile_caller_free (app->callers);
	app->callers = NULL;
    }

    list_store =
	gtk_list_store_new (4,
			    G_TYPE_STRING,
			    G_TYPE_DOUBLE,
			    G_TYPE_DOUBLE,
			    G_TYPE_POINTER);

    if (app->profile)
    {
	char *object = get_current_object (app);
	if (object)
	{
	    app->callers = profile_list_callers (app->profile, object);
	    add_callers (list_store, app->profile, app->callers);
	}
    }

    tree_view_set_model_with_default_sort (app->callers_view, GTK_TREE_MODEL (list_store),
					   CALLERS_TOTAL, GTK_SORT_DESCENDING);

    g_object_unref (G_OBJECT (list_store));

    gtk_tree_view_columns_autosize (app->callers_view);
}

static void
enter_display_mode (Application *app)
{
    app->state = DISPLAYING;

    update_sensitivity (app);

    app->inhibit_forced_redraw = TRUE;

    fill_main_list (app);

    /* This has the side effect of selecting the first row, which in turn causes
     * the other lists to be filled out
     */
    gtk_widget_grab_focus (GTK_WIDGET (app->object_view));

    app->inhibit_forced_redraw = FALSE;
}

static void
ensure_profile (Application *app)
{
    if (app->profile)
	return;

    collector_stop (app->collector);

    app->profile = collector_create_profile (app->collector);

    collector_reset (app->collector);

    enter_display_mode (app);
}

static void
on_about_activated (GtkWidget *widget, gpointer data)
{
#define OSLASH "\303\270"
    Application *app = data;
    char *name_property;

    if (gtk_minor_version >= 12)
	name_property = "program-name";
    else
	name_property = "name";

    gtk_show_about_dialog (GTK_WINDOW (app->main_window),
			   "logo", app->icon,
			   name_property, APPLICATION_NAME,
			   "copyright", "Copyright 2004-2009, S"OSLASH"ren Sandmann",
			   "version", PACKAGE_VERSION,
			   NULL);
}

static void
on_profile_toggled (GtkWidget *widget, gpointer data)
{
    Application *app = data;

    if (gtk_toggle_tool_button_get_active (GTK_TOGGLE_TOOL_BUTTON (app->profile_button)))
    {
	set_busy (app->main_window, TRUE);
	ensure_profile (app);
	set_busy (app->main_window, FALSE);
    }
}

static void
on_reset_clicked (gpointer widget, gpointer data)
{
    Application *app = data;

    set_busy (app->main_window, TRUE);

    delete_data (app);

    if (app->state == DISPLAYING)
    {
	app->state = INITIAL;
	collector_stop (app->collector);
    }

    update_sensitivity (app);

    set_busy (app->main_window, FALSE);
}

static gboolean
overwrite_file (GtkWindow *window,
		const char *filename)
{
    GtkWidget *msgbox;
    gchar *utf8_file_name;
    AtkObject *obj;
    gint ret;

    utf8_file_name = g_filename_to_utf8 (filename, -1, NULL, NULL, NULL);
    msgbox = gtk_message_dialog_new (window,
				     (GtkDialogFlags)GTK_DIALOG_DESTROY_WITH_PARENT,
				     GTK_MESSAGE_QUESTION,
				     GTK_BUTTONS_NONE,
				     _("A file named \"%s\" already exists."),
				     utf8_file_name);
    g_free (utf8_file_name);

    gtk_message_dialog_format_secondary_text (
	GTK_MESSAGE_DIALOG (msgbox),
	_("Do you want to replace it with the one you are saving?"));

    gtk_dialog_add_button (GTK_DIALOG (msgbox),
			   GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

    gtk_dialog_add_button (GTK_DIALOG (msgbox),
			   _("_Replace"), GTK_RESPONSE_YES);

    gtk_dialog_set_default_response (GTK_DIALOG (msgbox),
				     GTK_RESPONSE_CANCEL);

    obj = gtk_widget_get_accessible (msgbox);

    if (GTK_IS_ACCESSIBLE (obj))
	atk_object_set_name (obj, _("Question"));

    ret = gtk_dialog_run (GTK_DIALOG (msgbox));
    gtk_widget_destroy (msgbox);

    return (ret == GTK_RESPONSE_YES);
}

static void
on_save_as_clicked (gpointer widget,
		    gpointer data)
{
    Application *app = data;
    GtkWidget *dialog;

    ensure_profile (app);

    set_busy (app->main_window, TRUE);

    dialog = gtk_file_chooser_dialog_new ("Save As",
					  GTK_WINDOW (app->main_window),
					  GTK_FILE_CHOOSER_ACTION_SAVE,
					  GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					  GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
					  NULL);

    gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
    gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

    set_busy (app->main_window, FALSE);

retry:
    if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
    {
	GError *err = NULL;
	gchar *filename;

	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));

	if (g_file_test (filename, G_FILE_TEST_EXISTS)		&&
	    !overwrite_file (GTK_WINDOW (app->main_window), filename))
	{
	    g_free (filename);
	    goto retry;
	}

	set_busy (dialog, TRUE);
	if (!profile_save (app->profile, filename, &err))
	{
	    sorry (app->main_window, "Could not save %s: %s",
		   filename, err->message);

	    set_busy (dialog, FALSE);
	    g_free (filename);
	    goto retry;
	}
	set_application_title (app, filename);
	set_busy (dialog, FALSE);
	g_free (filename);
    }

    gtk_widget_destroy (dialog);
}

static void
set_loaded_profile (Application *app,
		    const char  *name,
		    Profile     *profile)
{
    g_return_if_fail (name != NULL);
    g_return_if_fail (profile != NULL);

    collector_stop (app->collector);

    delete_data (app);

    app->profile = profile;

    set_application_title (app, name);

    enter_display_mode (app);
}

static void
show_could_not_open (Application *app,
		     const char *filename,
		     GError *err)
{
    sorry (app->main_window,
	   "Could not open %s: %s",
	   filename,
	   err->message);
}

static void
on_open_clicked (gpointer widget,
		 gpointer data)
{
    Application *app = data;
    gchar *filename = NULL;
    Profile *profile = NULL;
    GtkWidget *dialog;

    set_busy (app->main_window, TRUE);

    dialog = gtk_file_chooser_dialog_new ("Open",
					  GTK_WINDOW (app->main_window),
					  GTK_FILE_CHOOSER_ACTION_OPEN,
					  GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					  GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
					  NULL);

    gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

    set_busy (app->main_window, FALSE);

retry:
    if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
    {
	GError *err = NULL;

	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));

	set_busy (dialog, TRUE);

	profile = profile_load (filename, &err);

	if (!profile)
	{
	    set_busy (dialog, FALSE);

	    show_could_not_open (app, filename, err);
	    g_error_free (err);
	    g_free (filename);

	    filename = NULL;
	    goto retry;
	}

	set_busy (dialog, FALSE);
    }

    gtk_widget_destroy (dialog);

    if (profile)
    {
	g_assert (filename);
	set_loaded_profile (app, filename, profile);

	g_free (filename);
    }
}

static void
on_delete (GtkWidget *window,
	   Application *app)
{
    /* Workaround for http://bugzilla.gnome.org/show_bug.cgi?id=317775
     *
     * Without it, the read callbacks can fire _after_ gtk_main_quit()
     * has been called and cause stuff to be called on destroyed widgets.
     */
    while (gtk_main_iteration ())
	;

    gtk_main_quit ();
}

static void
expand_descendants_tree (Application *app)
{
    GtkTreeModel *model = gtk_tree_view_get_model (app->descendants_view);
    GtkTreeIter iter;
    GList *all_paths = NULL;
    int n_rows;
    int max_rows = 40; /* FIXME */
    double top_value = 0.0;
    GtkTreePath *first_path;
    GList *list;

    first_path = gtk_tree_path_new_first();

    all_paths = g_list_prepend (all_paths, first_path);

    n_rows = 1;

    gtk_tree_model_get_iter (model, &iter, first_path);
    gtk_tree_model_get (model, &iter,
			OBJECT_TOTAL, &top_value,
			-1);

    while (all_paths && n_rows < max_rows)
    {
	GtkTreeIter best_iter;
	GtkTreePath *best_path;
	double best_value;
	int n_children;
	int i;

	best_value = 0.0;
	best_path = NULL;

	for (list = all_paths; list != NULL; list = list->next)
	{
	    GtkTreePath *path = list->data;
	    GtkTreeIter iter;

	    g_assert (path != NULL);

	    if (gtk_tree_model_get_iter (model, &iter, path))
	    {
		double value;
		gtk_tree_model_get (model, &iter,
				    OBJECT_TOTAL, &value,
				    -1);

		if (value >= best_value)
		{
		    best_value = value;
		    best_path = path;
		    best_iter = iter;
		}
	    }
	}

	n_children = gtk_tree_model_iter_n_children (model, &best_iter);

	if (n_children && (best_value / top_value) > 0.04 &&
	    (n_children + gtk_tree_path_get_depth (best_path)) /
	    (double)max_rows < (best_value / top_value) )
	{
	    gtk_tree_view_expand_row (
		GTK_TREE_VIEW (app->descendants_view), best_path, FALSE);
	    n_rows += n_children;

	    if (gtk_tree_path_get_depth (best_path) < 4)
	    {
		GtkTreePath *path = gtk_tree_path_copy (best_path);
		gtk_tree_path_down (path);

		for (i = 0; i < n_children; ++i)
		{
		    all_paths = g_list_prepend (all_paths, path);

		    path = gtk_tree_path_copy (path);
		    gtk_tree_path_next (path);
		}

		gtk_tree_path_free (path);
	    }
	}

	all_paths = g_list_remove (all_paths, best_path);

	if (!all_paths && n_rows == 1)
	{
	    /* Always expand at least once */
	    gtk_tree_view_expand_row (GTK_TREE_VIEW (app->descendants_view),
				      best_path, FALSE);
	}

	gtk_tree_path_free (best_path);
    }

    for (list = all_paths; list != NULL; list = list->next)
	gtk_tree_path_free (list->data);

    g_list_free (all_paths);
}

static void
get_data (GtkTreeView *view,
	  GtkTreeIter *iter,
	  gchar **name,
	  double *self,
	  double *cumulative)
{
    char *dummy1;
    double dummy2;
    double dummy3;

    GtkTreeModel *model = gtk_tree_view_get_model (view);
    gtk_tree_model_get (
	model, iter,
	DESCENDANTS_NAME, name? name : &dummy1,
	DESCENDANTS_SELF, self? self : &dummy2,
	DESCENDANTS_CUMULATIVE, cumulative? cumulative : &dummy3,
	-1);
}

static int
get_indent (GtkTreePath *path)
{
    return 2 * (gtk_tree_path_get_depth (path) - 1);
}

static void
compute_text_width (GtkTreeView  *view,
		    GtkTreePath  *path,
		    GtkTreeIter  *iter,
		    gpointer      data)
{
    int *width = data;
    char *name;

    get_data (view, iter, &name, NULL, NULL);

    *width = MAX (g_utf8_strlen (name, -1) + get_indent (path), *width);

    g_free (name);
}

typedef struct
{
    int max_width;
    GString *text;
} AddTextInfo;

static void
set_monospace (GtkWidget *widget)
{
    PangoFontDescription *desc =
	pango_font_description_from_string ("monospace");

    gtk_widget_modify_font (widget, desc);

    pango_font_description_free (desc);
}

static void
add_text (GtkTreeView *view,
	  GtkTreePath *path,
	  GtkTreeIter *iter,
	  gpointer     data)
{
    AddTextInfo *info = data;
    char *name;
    double self;
    double cumulative;
    int indent;
    int i;

    get_data (view, iter, &name, &self, &cumulative);

    indent = get_indent (path);

    for (i = 0; i < indent; ++i)
	g_string_append_c (info->text, ' ');

    g_string_append_printf (info->text, "%-*s %6.2f%% %6.2f%%\n",
			    info->max_width - indent, name, self, cumulative);

    g_free (name);
}

static gboolean
update_screenshot_window_idle (gpointer data)
{
    Application *app = data;
    GtkTextBuffer *text_buffer;

    if (!app->screenshot_window_visible)
	return FALSE;

    text_buffer =
	gtk_text_view_get_buffer (GTK_TEXT_VIEW (app->screenshot_textview));

    gtk_text_buffer_set_text (text_buffer, "", -1);

    if (app->descendants)
    {
	AddTextInfo info;

	info.max_width = 0;
	info.text = g_string_new ("");

	tree_view_foreach_visible (app->descendants_view,
				   compute_text_width,
				   &info.max_width);

	tree_view_foreach_visible (app->descendants_view,
				   add_text,
				   &info);

	gtk_text_buffer_set_text (text_buffer, info.text->str, -1);

	set_monospace (app->screenshot_textview);

	g_string_free (info.text, TRUE);
    }

    app->update_screenshot_id = 0;

    if (app->screenshot_window_visible)
    {
	set_busy (app->screenshot_window, FALSE);
	set_busy (app->screenshot_textview, FALSE);
    }

    return FALSE;
}

static void
update_screenshot_window (Application *app)
{
    /* We do this in an idle handler to deal with the case where
     * someone presses Shift-RightArrow on the root of a huge
     * profile. This causes a ton of 'expanded' notifications,
     * each of which would cause us to traverse the tree and
     * update the screenshot window.
     */
    if (app->update_screenshot_id)
	g_source_remove (app->update_screenshot_id);

    if (app->screenshot_window_visible)
    {
	/* don't swamp the X server with cursor change requests */
	if (!app->update_screenshot_id)
	{
	    set_busy (app->screenshot_window, TRUE);
	    set_busy (app->screenshot_textview, TRUE);
	}
    }

    app->update_screenshot_id = g_idle_add (
	update_screenshot_window_idle, app);
}

static void
on_descendants_row_expanded_or_collapsed (GtkTreeView *tree,
					  GtkTreeIter *iter,
					  GtkTreePath *path,
					  Application *app)
{
    update_screenshot_window (app);
}

static void
on_object_selection_changed (GtkTreeSelection *selection,
			     gpointer          data)
{
    Application *app = data;

    set_busy (app->main_window, TRUE);

    update_screenshot_window (app);

    if (!app->inhibit_forced_redraw)
	gdk_window_process_all_updates (); /* Display updated selection */

    fill_descendants_tree (app);
    fill_callers_list (app);

    if (get_current_object (app))
	expand_descendants_tree (app);

    set_busy (app->main_window, FALSE);
}

static void
really_goto_object (Application *app,
		    char *object)
{
    GtkTreeModel *profile_objects;
    GtkTreeIter iter;
    gboolean found = FALSE;

    profile_objects = gtk_tree_view_get_model (app->object_view);

    if (gtk_tree_model_get_iter_first (profile_objects, &iter))
    {
	do
	{
	    char *list_object;

	    gtk_tree_model_get (profile_objects, &iter,
				OBJECT_OBJECT, &list_object,
				-1);

	    if (list_object == object)
	    {
		found = TRUE;
		break;
	    }
	}
	while (gtk_tree_model_iter_next (profile_objects, &iter));
    }

    if (found)
    {
	GtkTreePath *path =
	    gtk_tree_model_get_path (profile_objects, &iter);

	gtk_tree_view_set_cursor (app->object_view, path, 0, FALSE);
    }
}

static void
goto_object (Application *app,
	     GtkTreeView *tree_view,
	     GtkTreePath *path,
	     gint	  column)
{
    GtkTreeIter iter;
    GtkTreeModel *model = gtk_tree_view_get_model (tree_view);
    char *object;

    if (!gtk_tree_model_get_iter (model, &iter, path))
	return;

    gtk_tree_model_get (model, &iter, column, &object, -1);

    if (!object)
	return;

    really_goto_object (app, object);
}

static void
on_descendants_row_activated (GtkTreeView *tree_view,
			      GtkTreePath *path,
			      GtkTreeViewColumn *column,
			      gpointer data)
{
    Application *app = data;

    goto_object (app, tree_view, path, DESCENDANTS_OBJECT);

    gtk_widget_grab_focus (GTK_WIDGET (app->descendants_view));
}

static void
on_callers_row_activated (GtkTreeView *tree_view,
			  GtkTreePath *path,
			  GtkTreeViewColumn *column,
			  gpointer data)
{
    Application *app = data;

    goto_object (app, tree_view, path, CALLERS_OBJECT);

    gtk_widget_grab_focus (GTK_WIDGET (app->callers_view));
}

static void
on_screenshot_activated (GtkCheckMenuItem *menu_item,
			 Application      *app)
{
    app->screenshot_window_visible = gtk_check_menu_item_get_active (menu_item);

    update_screenshot_window (app);

    update_sensitivity (app);
}

static void
on_screenshot_window_delete (GtkWidget   *window,
			     GdkEvent    *event,
			     Application *app)
{
    app->screenshot_window_visible = FALSE;

    update_sensitivity (app);
}

static void
on_screenshot_close_button_clicked (GtkWidget *widget,
				    Application *app)
{
    app->screenshot_window_visible = FALSE;

    update_sensitivity (app);
}

static void
set_sizes (GtkWindow *window,
	   GtkWindow *screenshot_window,
	   GtkWidget *hpaned,
	   GtkWidget *vpaned)
{
    GdkScreen *screen;
    int monitor_num;
    GdkRectangle monitor;
    int width, height;
    GtkWidget *widget = GTK_WIDGET (window);

    screen = gtk_widget_get_screen (widget);
    monitor_num = gdk_screen_get_monitor_at_window (screen, widget->window);

    gdk_screen_get_monitor_geometry (screen, monitor_num, &monitor);

    width = monitor.width * 3 / 4;
    height = monitor.height * 3 / 4;

    gtk_window_resize (window, width, height);

    gtk_paned_set_position (GTK_PANED (vpaned), height / 2);
    gtk_paned_set_position (GTK_PANED (hpaned), width * 3 / 8);

    width = monitor.width * 5 / 8;
    height = monitor.height * 5 / 8;

    gtk_window_resize (screenshot_window, width, height);
}

#define GLADE_FILE DATADIR "/sysprof.glade"

static void
gather_widgets (Application *app)
{
    typedef struct
    {
	void *location;
	const char *name;
    } WidgetInfo;

    const WidgetInfo widgets[] =
	{
	    { &app->main_window, "main_window" },
	    { &app->start_button, "start_button" },
	    { &app->profile_button, "profile_button" },
	    { &app->reset_button, "reset_button" },
	    { &app->save_as_button, "save_as_button" },
	    { &app->dummy_button, "dummy_button" },
	    { &app->samples_label, "samples_label" },
	    { &app->samples_hbox, "samples_hbox" },
	    { &app->start_item, "start_item" },
	    { &app->profile_item, "profile_item" },
	    { &app->reset_item, "reset_item" },
	    { &app->open_item, "open_item" },
	    { &app->save_as_item, "save_as_item" },
	    { &app->screenshot_item, "screenshot_item" },
	    { &app->quit_item, "quit" },
	    { &app->about_item, "about" },
	    { &app->object_view, "object_view" },
	    { &app->callers_view, "callers_view" },
	    { &app->descendants_view, "descendants_view" },
	    { &app->screenshot_window, "screenshot_window" },
	    { &app->screenshot_textview, "screenshot_textview" },
	    { &app->screenshot_close_button, "screenshot_close_button" },
	    { &app->vpaned, "vpaned" },
	    { &app->hpaned, "hpaned" },
	};

    GladeXML *xml = glade_xml_new (GLADE_FILE, NULL, NULL);
    int i;

    for (i = 0; i < G_N_ELEMENTS (widgets); ++i)
    {
	const WidgetInfo *info = &(widgets[i]);

	*(GtkWidget **)(info->location) = glade_xml_get_widget (xml, info->name);

	g_assert (GTK_IS_WIDGET (*(GtkWidget **)info->location));
    }

    g_object_unref (xml);
}

static void
connect_signals (Application *app)
{
    typedef struct
    {
	gpointer object;
	const char *signal;
	gpointer callback;
	gpointer data;
    } SignalInfo;

    const SignalInfo signals[] =
	{
	    { app->main_window, "delete_event", on_delete, NULL },
	    { app->start_button, "toggled", on_start_toggled, app },
	    { app->profile_button, "toggled", on_profile_toggled, app },
	    { app->reset_button, "clicked", on_reset_clicked, app },
	    { app->save_as_button, "clicked", on_save_as_clicked, app },
	    { app->start_item, "activate", on_menu_item_activated, app->start_button },
	    { app->profile_item, "activate", on_menu_item_activated, app->profile_button },
	    { app->reset_item, "activate", on_reset_clicked, app },
	    { app->open_item, "activate", on_open_clicked, app },
	    { app->save_as_item, "activate", on_save_as_clicked, app },
	    { app->screenshot_item, "activate", on_screenshot_activated, app },
	    { app->quit_item, "activate", on_delete, NULL },
	    { app->about_item, "activate", on_about_activated, app },
	    { app->object_selection, "changed", on_object_selection_changed, app },
	    { app->callers_view, "row-activated", on_callers_row_activated, app },
	    { app->descendants_view, "row-activated", on_descendants_row_activated, app },
	    { app->descendants_view, "row-expanded", on_descendants_row_expanded_or_collapsed, app },
	    { app->descendants_view, "row-collapsed", on_descendants_row_expanded_or_collapsed, app },
	    { app->screenshot_window, "delete_event", on_screenshot_window_delete, app },
	    { app->screenshot_close_button, "clicked", on_screenshot_close_button_clicked, app },
	};

    int i;

    for (i = 0; i < G_N_ELEMENTS (signals); ++i)
    {
	const SignalInfo *info = &(signals[i]);

	g_signal_connect (info->object, info->signal, info->callback, info->data);
    }
}

static void
set_shadows (void)
{
    /* Get rid of motif out-bevels */
    gtk_rc_parse_string (
	"style \"blah\" "
	"{ "
	"   GtkToolbar::shadow_type = none "
	"   GtkMenuBar::shadow_type = none "
	"   GtkMenuBar::internal_padding = 2 "
	"} "
	"widget \"*toolbar\" style : rc \"blah\"\n"
	"widget \"*menubar\" style : rc \"blah\"\n"
	);
}

static void
set_icons (Application *app)
{
    const char *icon_files [] = {
	PIXMAPDIR "/sysprof-icon-16.png",
	PIXMAPDIR "/sysprof-icon-24.png",
	PIXMAPDIR "/sysprof-icon-32.png",
	PIXMAPDIR "/sysprof-icon-48.png",
	NULL
    };
    GList *pixbufs = NULL;
    int i;

    for (i = 0; icon_files[i] != NULL; ++i)
    {
	const char *file = icon_files[i];
	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file (file, NULL);

	if (pixbuf)
	{
	    pixbufs = g_list_prepend (pixbufs, pixbuf);

	    if (i == 3) /* 48 x 48 */
		app->icon = g_object_ref (pixbuf);
	}
	else
	{
	    g_warning ("Could not open %s\n", file);
	}
    }

    gtk_window_set_icon_list (GTK_WINDOW (app->main_window), pixbufs);

    g_list_foreach (pixbufs, (GFunc)g_object_unref, NULL);
    g_list_free (pixbufs);
}

#define PCT_FORMAT "%.2f<span size='smaller'><span size='smaller'> </span>%%</span>"

static gboolean
build_gui (Application *app)
{
    GtkTreeViewColumn *col;

    set_shadows ();

    if (!g_file_test (GLADE_FILE, G_FILE_TEST_EXISTS))
    {
	sorry (NULL,
	       "Sysprof was not compiled or installed correctly.\n"
	       "\n"
	       "Running \"make install\" may solve this problem.\n");

	return FALSE;
    }

    gather_widgets (app);

    g_assert (app->main_window);

    /* Main Window */
    set_icons (app);

    gtk_widget_realize (GTK_WIDGET (app->main_window));

    /* Tool items */
    gtk_toggle_tool_button_set_active (
	GTK_TOGGLE_TOOL_BUTTON (app->profile_button), FALSE);

    /* TreeViews */

    /* object view */
    gtk_tree_view_set_enable_search (app->object_view, FALSE);
    col = add_plain_text_column (app->object_view, _("Functions"),
				 OBJECT_NAME);
    add_double_format_column (app->object_view, _("Self"),
			      OBJECT_SELF, PCT_FORMAT);
    add_double_format_column (app->object_view, _("Total"),
			      OBJECT_TOTAL, PCT_FORMAT);
    app->object_selection = gtk_tree_view_get_selection (app->object_view);
    gtk_tree_view_column_set_expand (col, TRUE);

    /* callers view */
    gtk_tree_view_set_enable_search (app->callers_view, FALSE);
    col = add_plain_text_column (app->callers_view, _("Callers"),
				 CALLERS_NAME);
    add_double_format_column (app->callers_view, _("Self"),
			      CALLERS_SELF, PCT_FORMAT);
    add_double_format_column (app->callers_view, _("Total"),
			      CALLERS_TOTAL, PCT_FORMAT);
    gtk_tree_view_column_set_expand (col, TRUE);

    /* descendants view */
    gtk_tree_view_set_enable_search (app->descendants_view, FALSE);
    col = add_plain_text_column (app->descendants_view, _("Descendants"),
				 DESCENDANTS_NAME);
    add_double_format_column (app->descendants_view, _("Self"),
			      DESCENDANTS_SELF, PCT_FORMAT);
    add_double_format_column (app->descendants_view, _("Cumulative"),
			      DESCENDANTS_CUMULATIVE, PCT_FORMAT);
    gtk_tree_view_column_set_expand (col, TRUE);

    /* screenshot window */

    /* set sizes */
    set_sizes (GTK_WINDOW (app->main_window),
	       GTK_WINDOW (app->screenshot_window),
	       app->hpaned, app->vpaned);

    /* hide/show widgets */
    gtk_widget_show_all (app->main_window);
    gtk_widget_hide (app->dummy_button);
    gtk_widget_hide (app->screenshot_window);

    gtk_widget_grab_focus (GTK_WIDGET (app->object_view));
    queue_show_samples (app);

    connect_signals (app);

    return TRUE;
}

static void
on_new_sample (gboolean first_sample,
	       gpointer data)
{
    Application *app = data;

    if (app->state == PROFILING && first_sample)
	update_sensitivity (app);
    else
	queue_show_samples (app);
}

static Application *
application_new (void)
{
    Application *app = g_new0 (Application, 1);

    app->collector = collector_new (FALSE, on_new_sample, app);
    app->state = INITIAL;

    return app;
}

typedef struct
{
    const char *filename;
    Application *app;
} FileOpenData;

static gboolean
load_file (gpointer data)
{
    FileOpenData *file_open_data = data;
    const char *filename = file_open_data->filename;
    Application *app = file_open_data->app;
    GError *err = NULL;
    Profile *profile;

    set_busy (app->main_window, TRUE);

    profile = profile_load (filename, &err);

    if (profile)
    {
	set_loaded_profile (app, filename, profile);

	gdk_window_process_all_updates ();
	set_busy (app->main_window, FALSE);
    }
    else
    {
	set_busy (app->main_window, FALSE);

	show_could_not_open (app, filename, err);
	g_error_free (err);
    }

    g_free (file_open_data);

    return FALSE;
}

static const char *
process_options (int           argc,
		 char        **argv)
{
    int i;
    gboolean show_version = FALSE;
    const char *filename = NULL;

    for (i = 1; i < argc; ++i)
    {
	char *option = argv[i];

	if (strcmp (option, "--version") == 0)
	{
	    show_version = TRUE;
	}
	else if (!filename)
	{
	    filename = argv[i];
	}
    }

    if (show_version)
    {
	g_print ("%s %s\n", APPLICATION_NAME, PACKAGE_VERSION);

	exit (1);
    }

    return filename;
}

static void
apply_workarounds (void)
{

    /* Disable gslice, since it
     *
     *  - confuses valgrind
     *  - caches too much memory
     *  - hides memory access bugs
     *  - is not faster than malloc
     *
     * Note that g_slice_set_config() is broken in some versions of
     * GLib (and 'declared internal' according to Tim), so we use the
     * environment variable instead.
     */
    if (!getenv ("G_SLICE"))
	putenv ("G_SLICE=always_malloc");

    /* Accessibility prevents sysprof from working reliably, so
     * disable it. Specifically, it
     *
     *  - causes large amounts of time to be spent in sysprof itself
     *    whenever the label is updated.
     *  - sometimes hangs at shutdown
     *  - does long-running roundtrip requests that prevents
     *    reading the event buffers, resulting in lost events.
     */
    putenv ("NO_GAIL=1");
    putenv ("NO_AT_BRIDGE=1");
}

int
main (int    argc,
      char **argv)
{
    Application *app;
    const char *filename;

    apply_workarounds();

    filename = process_options (argc, argv);

    gtk_init (&argc, &argv);

    app = application_new ();

    if (!build_gui (app))
	return -1;

    update_sensitivity (app);

    if (filename)
    {
	FileOpenData *file_open_data = g_new0 (FileOpenData, 1);
	file_open_data->filename = filename;
	file_open_data->app = app;

	/* This has to run at G_PRIORITY_LOW because of bug 350517
	 */
	g_idle_add_full (G_PRIORITY_LOW, load_file, file_open_data, NULL);
    }

    gtk_main ();

    return 0;
}
