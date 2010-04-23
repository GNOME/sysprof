/* Sysprof -- Sampling, systemwide CPU profiler
 * Copyright 2005, Lorenzo Colitti
 * Copyright 2005, Soren Sandmann 
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <glib.h>
#include <stdio.h>

#include "stackstash.h"
#include "profile.h"
#include "watch.h"
#include "signal-handler.h"
#include "collector.h"

typedef struct Application Application;
struct Application
{
    Collector *	collector;
    char *	outfile;
    GMainLoop * main_loop;
};

static void
dump_data (Application *app)
{
    GError *err = NULL;
    Profile *profile;

    printf ("Saving profile (%d samples) in %s ... ",
	    collector_get_n_samples (app->collector),
	    app->outfile);
    fflush (stdout);

    collector_stop (app->collector);
    
    profile = collector_create_profile (app->collector);
    profile_save (profile, app->outfile, &err);
    
    if (err)
    {
	printf ("failed\n");
        fprintf (stderr, "Error saving %s: %s\n", app->outfile, err->message);
        exit (1);
    }
    else
    {
	printf ("done\n\n");
    }
}

static void
signal_handler (int      signo,
		gpointer data)
{
    Application *app = data;
    
    dump_data (app);
    
    while (g_main_context_iteration (NULL, FALSE))
	;
    
    g_main_loop_quit (app->main_loop);
}

static char *
usage_msg (const char *name)
{
    return g_strdup_printf (
	"Usage: \n"
	"    %s <outfile>\n"
	"\n"
	"On SIGTERM or SIGINT (Ctrl-C) the profile will be written to <outfile>",
	name);
}

static gboolean
file_exists_and_is_dir (const char *name)
{
    return
	g_file_test (name, G_FILE_TEST_EXISTS) &&
	g_file_test (name, G_FILE_TEST_IS_DIR);
}

static void
die (const char *err_msg)
{
    if (err_msg)
	fprintf (stderr, "\n%s\n\n", err_msg);
    
    exit (-1);
}

int
main (int argc, char *argv[])
{
    Application *app = g_new0 (Application, 1);
    GError *err;
    
    app->collector = collector_new (FALSE, NULL, NULL);
    app->outfile = g_strdup (argv[1]);
    app->main_loop = g_main_loop_new (NULL, 0);
    
    err = NULL;
    
    if (!collector_start (app->collector, &err))
	die (err->message);
    
    if (argc < 2)
	die (usage_msg (argv[0]));
    
    if (!signal_set_handler (SIGTERM, signal_handler, app, &err))
	die (err->message);
    
    if (!signal_set_handler (SIGINT, signal_handler, app, &err))
	die (err->message);

    if (file_exists_and_is_dir (app->outfile))
    {
	char *msg = g_strdup_printf ("Can't write to %s: is a directory\n",
				     app->outfile);
	die (msg);
    }
    
    g_main_loop_run (app->main_loop);
    
    signal_unset_handler (SIGTERM);
    signal_unset_handler (SIGINT);
    
    return 0;
}
