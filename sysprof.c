#include <stdio.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <glade/glade.h>
#include <errno.h>

#include "binfile.h"
#include "watch.h"
#include "sysprof-module.h"
#include "stackstash.h"
#include "profile.h"
#include "treeviewutils.h"

/* FIXME */
#define _(a) a

typedef struct Application Application;

typedef enum
{
    INITIAL,
    DISPLAYING,
    PROFILING
} State;

struct Application
{
    int			input_fd;
    State		state;
    StackStash *	stash;

    GtkTreeView *	object_view;
    GtkTreeView *	callers_view;
    GtkTreeView *	descendants_view;
    GtkStatusbar *	statusbar;

    GtkWidget *		start_button;
    GtkWidget *		profile_button;
    GtkWidget *		reset_button;
    GtkWidget *		save_as_button;
    GtkWidget *		dummy_button;
    
    GtkWidget *		start_item;
    GtkWidget *		profile_item;
    GtkWidget *		open_item;
    GtkWidget *		save_as_item;
    
    Profile *		profile;
    ProfileDescendant * descendants;
    ProfileCaller *	callers;

    int			n_samples;

    int			timeout_id;
    int			generating_profile;
};

static void
disaster (const char *what)
{
    fprintf (stderr, what);
    exit (1);
}

static void
update_sensitivity (Application *app)
{
    gboolean sensitive_profile_button;
    gboolean sensitive_save_as_button;
    gboolean sensitive_start_button;
    gboolean sensitive_tree_views;

    GtkWidget *active_radio_button;

    switch (app->state)
    {
    case INITIAL:
	sensitive_profile_button = FALSE;
	sensitive_save_as_button = FALSE;
	sensitive_start_button = TRUE;
	sensitive_tree_views = FALSE;
	active_radio_button = app->dummy_button;
	break;

    case PROFILING:
	sensitive_profile_button = (app->n_samples > 0);
	sensitive_save_as_button = (app->n_samples > 0);
	sensitive_start_button = TRUE;
	sensitive_tree_views = FALSE;
	active_radio_button = app->start_button;
	break;

    case DISPLAYING:
	sensitive_profile_button = TRUE;
	sensitive_save_as_button = TRUE;
	sensitive_start_button = TRUE;
	sensitive_tree_views = TRUE;
	active_radio_button = app->profile_button;
	break;
    }

    gtk_widget_set_sensitive (GTK_WIDGET (app->profile_button),
			      sensitive_profile_button);

    gtk_toggle_tool_button_set_active (GTK_TOGGLE_TOOL_BUTTON (active_radio_button), TRUE);

    gtk_widget_set_sensitive (GTK_WIDGET (app->save_as_button),
			      sensitive_save_as_button);

    gtk_widget_set_sensitive (GTK_WIDGET (app->start_button),
			      sensitive_start_button);

    gtk_widget_set_sensitive (GTK_WIDGET (app->object_view), sensitive_tree_views);
    gtk_widget_set_sensitive (GTK_WIDGET (app->callers_view), sensitive_tree_views);
    gtk_widget_set_sensitive (GTK_WIDGET (app->descendants_view), sensitive_tree_views);
}

#if 0
static gchar *
get_name (pid_t pid)
{
    char *cmdline;
    char *name = g_strdup_printf ("/proc/%d/cmdline", pid);
    
    if (g_file_get_contents (name, &cmdline, NULL, NULL))
	return cmdline;
    else
	return g_strdup ("<unknown>");
}
#endif

static gboolean
really_show_samples (gpointer data)
{
    Application *app = data;
    char *label;
    
    label = g_strdup_printf ("Samples: %d", app->n_samples);

    gtk_statusbar_pop (app->statusbar, 0);
    gtk_statusbar_push (app->statusbar, 0, label);

    g_free (label);

    app->timeout_id = 0;
    return FALSE;
}

static void
show_samples (Application *app)
{
    if (!app->timeout_id)
	app->timeout_id = g_timeout_add (300, really_show_samples, app);
}

static void
on_read (gpointer data)
{
    Application *app = data;
    SysprofStackTrace trace;
    int rd;
    
    rd = read (app->input_fd, &trace, sizeof (trace));

    if (app->state != PROFILING)
	return;
    
    if (rd == -1 && errno == EWOULDBLOCK)
	return;
    
#if 0
    g_print ("pid: %d\n", trace.pid);
    for (i=0; i < trace.n_addresses; ++i)
	g_print ("rd: %08x\n", trace.addresses[i]);
    g_print ("-=-\n");
#endif

    if (rd > 0  && !app->generating_profile)
    {
	Process *process = process_get_from_pid (trace.pid);
	int i;
/* 	char *filename = NULL; */

/* 	if (*trace.filename) */
/* 	    filename = trace.filename; */
	
	for (i = 0; i < trace.n_addresses; ++i)
	    process_ensure_map (process, trace.pid, 
				(gulong)trace.addresses[i]);
	g_assert (!app->generating_profile);

	stack_stash_add_trace (
	    app->stash, process,
	    (gulong *)trace.addresses, trace.n_addresses, 1);
	
	app->n_samples++;
	show_samples (app);
    }

    update_sensitivity (app);
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

    if (app->stash)
	stack_stash_free (app->stash);
    app->stash = stack_stash_new ();
    process_flush_caches ();
    app->n_samples = 0;
    show_samples (app);
}

static void
on_start_toggled (GtkToggleToolButton *tool_button, gpointer data)
{
    Application *app = data;

    if (!gtk_toggle_tool_button_get_active (tool_button))
	return;
    
    delete_data (app);
    app->state = PROFILING;
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
    DESCENDANTS_NON_RECURSE,
    DESCENDANTS_TOTAL,
    DESCENDANTS_OBJECT
};

static void
fill_main_list (Application *app)
{
    GList *list;
    GtkListStore *list_store;
    Profile *profile = app->profile;
    
    if (profile)
    {
	gpointer sort_state;
	
	list_store = gtk_list_store_new (4,
					 G_TYPE_STRING,
					 G_TYPE_DOUBLE,
					 G_TYPE_DOUBLE,
					 G_TYPE_POINTER);
	
	for (list = profile->objects; list != NULL; list = list->next)
	{
	    ProfileObject *object = list->data;
	    GtkTreeIter iter;
	    
	    gtk_list_store_append (list_store, &iter);
	    
	    gtk_list_store_set (list_store, &iter,
				OBJECT_NAME, object->name,
				OBJECT_SELF, 100.0 * object->self/(double)profile->profile_size,
				OBJECT_TOTAL, 100.0 * object->total/(double)profile->profile_size,
				OBJECT_OBJECT, object,
				-1);
	}

	sort_state = save_sort_state (app->object_view);
	
	gtk_tree_view_set_model (app->object_view, GTK_TREE_MODEL (list_store));

	if (sort_state)
	{
	    restore_sort_state (app->object_view, sort_state);
	}
	else
	{
	    gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (list_store),
						  OBJECT_TOTAL,
						  GTK_SORT_DESCENDING);
	}
	
	g_object_unref (G_OBJECT (list_store));
    }
}

static void
on_profile_toggled (gpointer widget, gpointer data)
{
    Application *app = data;

    if (app->generating_profile || !gtk_toggle_tool_button_get_active (widget))
	return;

    if (app->profile)
	profile_free (app->profile);

    /* take care of reentrancy */
    app->generating_profile = TRUE;
    
    app->profile = profile_new (app->stash);

    app->generating_profile = FALSE;
    
    fill_main_list (app);

    app->state = DISPLAYING;

#if 0
    gtk_tree_view_columns_autosize (app->object_view);
    gtk_tree_view_columns_autosize (app->callers_view);
    gtk_tree_view_columns_autosize (app->descendants_view);
#endif
    
    update_sensitivity (app);
}

static void
on_open_clicked (gpointer widget, gpointer data)
{
    
}

static void
on_reset_clicked (gpointer widget, gpointer data)
{
    Application *app = data;

    delete_data (app);

    if (app->state == DISPLAYING)
	app->state = INITIAL;
    
    update_sensitivity (app);
}

static void
on_save_as_clicked (gpointer widget, gpointer data)
{
    Application *app = data;


    if (app)
	;
    /* FIXME */
}

static void
on_delete (GtkWidget *window)
{
    gtk_main_quit ();
}

static void
add_node (GtkTreeStore      *store,
	  int                size,
	  const GtkTreeIter *parent,
	  ProfileDescendant *node)
{
    GtkTreeIter iter;

    if (!node)
	return;
    
    gtk_tree_store_insert (store, &iter, (GtkTreeIter *)parent, 0);

    gtk_tree_store_set (store, &iter,
			DESCENDANTS_NAME, node->object->name,
			DESCENDANTS_SELF, 100 * (node->self)/(double)size,
			DESCENDANTS_NON_RECURSE, 100 * (node->non_recursion)/(double)size,
			DESCENDANTS_TOTAL, 100 * (node->total)/(double)size,
			DESCENDANTS_OBJECT, node->object,
			-1);

    add_node (store, size, parent, node->siblings);
    add_node (store, size, &iter, node->children);
}

static ProfileObject *
get_current_object (Application *app)
{
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreeIter selected;
    ProfileObject *object;

    selection = gtk_tree_view_get_selection (app->object_view);
    
    gtk_tree_selection_get_selected (selection, &model, &selected);
    
    gtk_tree_model_get (model, &selected,
			OBJECT_OBJECT, &object,
			-1);

    return object;
}

static void
fill_descendants_tree (Application *app)
{
    GtkTreeStore *tree_store;
    gpointer sort_state;
    
    sort_state = save_sort_state (app->descendants_view);
    
    if (app->descendants)
    {
	profile_descendant_free (app->descendants);
	app->descendants = NULL;
    }
    
    tree_store =
	gtk_tree_store_new (5,
			    G_TYPE_STRING,
			    G_TYPE_DOUBLE,
			    G_TYPE_DOUBLE,
			    G_TYPE_DOUBLE,
			    G_TYPE_POINTER);

    if (app->profile)
    {
	ProfileObject *object = get_current_object (app);
	if (object)
	{
	    app->descendants =
		profile_create_descendants (app->profile, object);
	    add_node (tree_store,
		      app->profile->profile_size, NULL, app->descendants);
	}
    }

    gtk_tree_view_set_model (
	app->descendants_view, GTK_TREE_MODEL (tree_store));

    g_object_unref (G_OBJECT (tree_store));
    
    if (!sort_state)
    {
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (tree_store),
					      DESCENDANTS_NON_RECURSE,
					      GTK_SORT_DESCENDING);
    }
    else
    {
	restore_sort_state (app->descendants_view, sort_state);
    }
}

static void
add_callers (GtkListStore *list_store,
	     Profile *profile,
	     ProfileCaller *callers)
{
    while (callers)
    {
	gchar *name;
	GtkTreeIter iter;
	
	if (callers->object)
	    name = callers->object->name;
	else
	    name = "<spontaneous>";

	gtk_list_store_append (list_store, &iter);
	gtk_list_store_set (
	    list_store, &iter,
	    CALLERS_NAME, name,
	    CALLERS_SELF, 100.0 * callers->self/(double)profile->profile_size,
	    CALLERS_TOTAL, 100.0 * callers->total/(double)profile->profile_size,
	    CALLERS_OBJECT, callers->object,
	    -1);

	callers = callers->next;
    }
}

static void
fill_callers_list (Application *app)
{
    GtkListStore *list_store;
    gpointer sort_state;
    
    sort_state = save_sort_state (app->descendants_view);
    
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
	ProfileObject *object = get_current_object (app);
	if (object)
	{
	    app->callers = profile_list_callers (app->profile, object);
	    add_callers (list_store, app->profile, app->callers);
	}
    }

    gtk_tree_view_set_model (
	app->callers_view, GTK_TREE_MODEL (list_store));
    
    g_object_unref (G_OBJECT (list_store));
    
    if (!sort_state)
    {
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (list_store),
					      CALLERS_TOTAL,
					      GTK_SORT_DESCENDING);
    }
    else
    {
	restore_sort_state (app->callers_view, sort_state);
    }
}

static void
on_object_selection_changed (GtkTreeSelection *selection, gpointer data)
{
    Application *app = data;
    GtkTreePath *path;
    
    fill_descendants_tree (app);
    fill_callers_list (app);

    /* Expand the toplevel of the descendant tree so we see the immediate
     * descendants.
     */
    path = gtk_tree_path_new_from_indices (0, -1);
    gtk_tree_view_expand_row (
	GTK_TREE_VIEW (app->descendants_view), path, FALSE);
    gtk_tree_path_free (path);
}

static void
really_goto_object (Application *app, ProfileObject *object)
{
    GtkTreeModel *profile_objects;
    GtkTreeIter iter;
    gboolean found = FALSE;
    
    profile_objects = gtk_tree_view_get_model (app->object_view);

    if (gtk_tree_model_get_iter_first (profile_objects, &iter))
    {
	do
	{
	    ProfileObject *profile_object;
	    
	    gtk_tree_model_get (profile_objects, &iter,
				OBJECT_OBJECT, &profile_object,
				-1);
	    
	    if (profile_object == object)
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
    
    gtk_widget_grab_focus (GTK_WIDGET (app->object_view));
}

static void
goto_object (Application *app,
	     GtkTreeView *tree_view,
	     GtkTreePath *path,
	     gint	  column)
{
    GtkTreeIter iter;
    GtkTreeModel *model = gtk_tree_view_get_model (tree_view);
    ProfileObject *object;

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
    goto_object (data, tree_view, path, DESCENDANTS_OBJECT);
}

static void
on_callers_row_activated (GtkTreeView *tree_view,
			  GtkTreePath *path,
			  GtkTreeViewColumn *column,
			  gpointer data)
{
    goto_object (data, tree_view, path, CALLERS_OBJECT);
}

static void
set_sizes (GtkWindow *window,
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
    gtk_paned_set_position (GTK_PANED (hpaned), width / 2);
}

static void
build_gui (Application *app)
{
    GladeXML *xml;
    GtkWidget *main_window;
    GtkTreeSelection *selection;
    GtkTreeViewColumn *col;
    
    xml = glade_xml_new ("./sysprof.glade", NULL, NULL);

    /* Main Window */
    main_window = glade_xml_get_widget (xml, "main_window");
    
    g_signal_connect (G_OBJECT (main_window), "delete_event",
		      G_CALLBACK (on_delete), NULL);

    gtk_widget_show_all (main_window);

    /* Menu items */
    app->start_item = glade_xml_get_widget (xml, "start_item");
    app->profile_item = glade_xml_get_widget (xml, "profile_item");
    app->open_item = glade_xml_get_widget (xml, "open_item");
    app->save_as_item = glade_xml_get_widget (xml, "save_as_item");

    g_assert (app->start_item);
    g_assert (app->profile_item);
    g_assert (app->save_as_item);
    g_assert (app->open_item);
    
    g_signal_connect (G_OBJECT (app->start_item), "activate",
		      G_CALLBACK (on_start_toggled), app);

    g_signal_connect (G_OBJECT (app->profile_item), "activate",
		      G_CALLBACK (on_profile_toggled), app);
    
    g_signal_connect (G_OBJECT (app->open_item), "activate",
		      G_CALLBACK (on_open_clicked), app);
    
    g_signal_connect (G_OBJECT (app->save_as_item), "activate",
		      G_CALLBACK (on_save_as_clicked), app);

    /* quit */
    g_signal_connect (G_OBJECT (glade_xml_get_widget (xml, "quit_item")), "activate",
		      G_CALLBACK (on_delete), NULL);
    
    /* Tool items */
    
    app->start_button = glade_xml_get_widget (xml, "start_button");
    app->profile_button = glade_xml_get_widget (xml, "profile_button");
    app->reset_button = glade_xml_get_widget (xml, "reset_button");
    app->save_as_button = glade_xml_get_widget (xml, "save_as_button");
    app->dummy_button = glade_xml_get_widget (xml, "dummy_button");

    gtk_widget_hide (app->dummy_button);
    
    gtk_toggle_tool_button_set_active (GTK_TOGGLE_TOOL_BUTTON (
					   app->profile_button), FALSE);
    
    g_signal_connect (G_OBJECT (app->start_button), "toggled",
		      G_CALLBACK (on_start_toggled), app);

    g_signal_connect (G_OBJECT (app->profile_button), "toggled",
		      G_CALLBACK (on_profile_toggled), app);

    g_signal_connect (G_OBJECT (app->reset_button), "clicked",
		      G_CALLBACK (on_reset_clicked), app);

    g_signal_connect (G_OBJECT (app->save_as_button), "clicked",
		      G_CALLBACK (on_save_as_clicked), app);
    
    gtk_widget_realize (GTK_WIDGET (main_window));
    set_sizes (GTK_WINDOW (main_window),
	       glade_xml_get_widget (xml, "hpaned"),
	       glade_xml_get_widget (xml, "vpaned"));
    
    
    /* TreeViews */

    /* object view */
    app->object_view = (GtkTreeView *)glade_xml_get_widget (xml, "object_view");
    col = add_plain_text_column (app->object_view, _("Name"), OBJECT_NAME);
    add_double_format_column (app->object_view, _("Self"), OBJECT_SELF, "%.2f");
    add_double_format_column (app->object_view, _("Total"), OBJECT_TOTAL, "%.2f");
    selection = gtk_tree_view_get_selection (app->object_view);
    g_signal_connect (selection, "changed", G_CALLBACK (on_object_selection_changed), app);
    
    gtk_tree_view_column_set_expand (col, TRUE);
    
    /* callers view */
    app->callers_view = (GtkTreeView *)glade_xml_get_widget (xml, "callers_view");
    col = add_plain_text_column (app->callers_view, _("Name"), CALLERS_NAME);
    add_double_format_column (app->callers_view, _("Self"), CALLERS_SELF, "%.2f");
    add_double_format_column (app->callers_view, _("Total"), CALLERS_TOTAL, "%.2f");
    g_signal_connect (app->callers_view, "row-activated",
		      G_CALLBACK (on_callers_row_activated), app);

    gtk_tree_view_column_set_expand (col, TRUE);
    
    /* descendants view */
    app->descendants_view = (GtkTreeView *)glade_xml_get_widget (xml, "descendants_view");
    col = add_plain_text_column (app->descendants_view, _("Name"), DESCENDANTS_NAME);
    add_double_format_column (app->descendants_view, _("Self"), DESCENDANTS_SELF, "%.2f");
    add_double_format_column (app->descendants_view, _("Cummulative"), DESCENDANTS_NON_RECURSE, "%.2f");
    g_signal_connect (app->descendants_view, "row-activated",
		      G_CALLBACK (on_descendants_row_activated), app);

    gtk_tree_view_column_set_expand (col, TRUE);

    /* Statusbar */
    app->statusbar = (GtkStatusbar *)glade_xml_get_widget (xml, "statusbar");
    show_samples (app);
}

static Application *
application_new (void)
{
    Application *app = g_new0 (Application, 1);
    
    app->stash = stack_stash_new ();
    app->input_fd = -1;
    app->state = INITIAL;
    
    return app;
}

int
main (int argc, char **argv)
{
    Application *app;
    
    gtk_init (&argc, &argv);
    
    app = application_new ();
    
    app->input_fd = open ("/proc/sysprof-trace", O_RDONLY);
    if (app->input_fd < 0)
    {
	disaster ("Can't open /proc/sysprof-trace. You need to insert\n"
		  "the sysprof kernel module. As root type\n"
		  "\n"
		  "       insmod sysprof-module.o\n"
		  "\n"
		  "or if you are using a 2.6 kernel:\n"
		  "\n"
		  "       insmod sysprof-module.ko\n"
		  "\n");
    }

    fd_add_watch (app->input_fd, app);
    fd_set_read_callback (app->input_fd, on_read);
    
    build_gui (app);

    update_sensitivity (app);

    gtk_main ();
    
    return 0;
}
