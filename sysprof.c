#include <stdio.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <glade/glade.h>

#include "binfile.h"
#include "watch.h"
#include "sysprof-module.h"
#include "stackstash.h"
#include "profile.h"
#include "treeviewutils.h"

/* FIXME */
#define _(a) a

typedef struct Application Application;

struct Application
{
    int			input_fd;
    StackStash *	stash;
    GList *		page_faults;
    GtkTreeView *	object_view;
    GtkTreeView *	callers_view;
    GtkTreeView *	descendants_view;
    GtkStatusbar *	statusbar;

    GtkToolItem *	profile_button;
    GtkToolItem *	reset_button;
    
    Profile *		profile;
    ProfileDescendant * descendants;
    ProfileCaller *	callers;

    int			n_samples;
    gboolean		profiling;

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
    gboolean sensitive_profile_button = (app->n_samples != 0);

    gtk_widget_set_sensitive (GTK_WIDGET (app->profile_button),
			      sensitive_profile_button);
#if 0
    gtk_widget_set_sensitive (GTK_WIDGET (app->reset_button),
			      sensitive_profile_button);
#endif
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
    int i;
    
    rd = read (app->input_fd, &trace, sizeof (trace));

#if 0
    g_print ("pid: %d\n", trace.pid);
    for (i=0; i < trace.n_addresses; ++i)
	g_print ("rd: %08x\n", trace.addresses[i]);
    g_print ("-=-\n");
#endif

    if (rd > 0 && app->profiling && !app->generating_profile)
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
on_reset (GtkWidget *widget, gpointer data)
{
    Application *app = data;

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
on_profile (gpointer widget, gpointer data)
{
    Application *app = data;

    if (app->generating_profile)
	return;
    
    if (app->profile)
	profile_free (app->profile);

    /* take care of reentrancy */
    app->generating_profile = TRUE;
    
    app->profile = profile_new (app->stash);

    app->generating_profile = FALSE;
    
    fill_main_list (app);
}

static void
on_delete (GtkWidget *window)
{
    gtk_main_quit ();
}

static void
on_start_toggled (GtkToggleToolButton *tool_button, gpointer data)
{
    Application *app = data;

    app->profiling = gtk_toggle_tool_button_get_active (tool_button);

    update_sensitivity (app);
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
add_stock (const char *stock_id,
	   const char *label, guint size, const guint8 *data)
{
    GdkPixbuf *pixbuf;
    GtkIconSet *icon_set;
    GtkIconFactory *icon_factory;
    GtkStockItem stock_item;

    pixbuf = gdk_pixbuf_new_from_inline (size, data, FALSE, NULL);
    icon_set = gtk_icon_set_new_from_pixbuf (pixbuf);
    icon_factory = gtk_icon_factory_new ();
    gtk_icon_factory_add (icon_factory, stock_id, icon_set);
    gtk_icon_factory_add_default (icon_factory);
    g_object_unref (G_OBJECT (icon_factory));
    gtk_icon_set_unref (icon_set);
    stock_item.stock_id = (char *)stock_id;
    stock_item.label = (char *)label;
    stock_item.modifier = 0;
    stock_item.keyval = 0;
    stock_item.translation_domain = NULL;
    gtk_stock_add (&stock_item, 1);
}

static void
build_gui (Application *app)
{
    GladeXML *xml;
    GtkWidget *main_window;
    GtkWidget *main_vbox;
    GtkWidget *toolbar;
    GtkToolItem *item;
    GtkTreeSelection *selection;
    
    xml = glade_xml_new ("./sysprof.glade", NULL, NULL);
    
    /* Main Window */
    main_window = glade_xml_get_widget (xml, "main_window");
    
    g_signal_connect (G_OBJECT (main_window), "delete_event",
		      G_CALLBACK (on_delete), NULL);
    gtk_window_set_default_size (GTK_WINDOW (main_window), 640, 400);
    gtk_widget_show_all (main_window);
    
    /* Toolbar */
    main_vbox = glade_xml_get_widget (xml, "main_vbox");
    toolbar = gtk_toolbar_new ();

    /* Stock Items */
#include "pixbufs.c"
    add_stock ("sysprof-start-profiling", "Star_t",
	       sizeof (start_profiling), start_profiling);
    add_stock ("sysprof-stop-profiling", "Sto_p",
	       sizeof (stop_profiling), stop_profiling);

    /* Stop */
    item = gtk_radio_tool_button_new_from_stock (
	NULL, "sysprof-stop-profiling");
    gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -11);
    
    /* Start */
    item = gtk_radio_tool_button_new_with_stock_from_widget (
	GTK_RADIO_TOOL_BUTTON (item), "sysprof-start-profiling");
    gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, 0);
    g_signal_connect (G_OBJECT (item), "toggled",
		      G_CALLBACK (on_start_toggled), app);

    /* Reset */
    item = gtk_tool_button_new_from_stock (GTK_STOCK_CLEAR);
    gtk_tool_button_set_label (GTK_TOOL_BUTTON (item), "_Reset");
    gtk_tool_button_set_use_underline (GTK_TOOL_BUTTON (item), TRUE);
    gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);
    g_signal_connect (G_OBJECT (item), "clicked",
		      G_CALLBACK (on_reset), app);

    app->reset_button = item;

    /* Separator */
    item = gtk_separator_tool_item_new ();
    gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);
    
    /* Profile */
    item = gtk_tool_button_new_from_stock (GTK_STOCK_JUSTIFY_LEFT);
    gtk_tool_button_set_label (GTK_TOOL_BUTTON (item), "_Profile");
    gtk_tool_button_set_use_underline (GTK_TOOL_BUTTON (item), TRUE);
    gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);
    g_signal_connect (G_OBJECT (item), "clicked",
		      G_CALLBACK (on_profile), app);

    app->profile_button = item;

    /* Show toolbar */
    gtk_widget_show_all (GTK_WIDGET (toolbar));
    
    /* Add toolbar to vbox */
    gtk_container_add (GTK_CONTAINER (main_vbox), toolbar);
    gtk_box_reorder_child (GTK_BOX (main_vbox), toolbar, 1);
    gtk_box_set_child_packing (GTK_BOX (main_vbox), toolbar,
			       FALSE, TRUE, 0, GTK_PACK_START);
    
    /* TreeViews */
    /* object view */
    app->object_view = (GtkTreeView *)glade_xml_get_widget (xml, "object_view");
    add_plain_text_column (app->object_view, _("Name"), OBJECT_NAME);
    add_double_format_column (app->object_view, _("Self"), OBJECT_SELF, "%.2f");
    add_double_format_column (app->object_view, _("Total"), OBJECT_TOTAL, "%.2f");
    selection = gtk_tree_view_get_selection (app->object_view);
    g_signal_connect (selection, "changed", G_CALLBACK (on_object_selection_changed), app);
    
    /* callers view */
    app->callers_view = (GtkTreeView *)glade_xml_get_widget (xml, "callers_view");
    add_plain_text_column (app->callers_view, _("Name"), CALLERS_NAME);
    add_double_format_column (app->callers_view, _("Self"), CALLERS_SELF, "%.2f");
    add_double_format_column (app->callers_view, _("Total"), CALLERS_TOTAL, "%.2f");
    g_signal_connect (app->callers_view, "row-activated",
		      G_CALLBACK (on_callers_row_activated), app);
    
    /* descendants view */
    app->descendants_view = (GtkTreeView *)glade_xml_get_widget (xml, "descendants_view");
    add_plain_text_column (app->descendants_view, _("Name"), DESCENDANTS_NAME);
    add_double_format_column (app->descendants_view, _("Self"), DESCENDANTS_SELF, "%.2f");
    add_double_format_column (app->descendants_view, _("Cummulative"), DESCENDANTS_NON_RECURSE, "%.2f");
    g_signal_connect (app->descendants_view, "row-activated",
		      G_CALLBACK (on_descendants_row_activated), app);

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
