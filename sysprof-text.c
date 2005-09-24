/* Sysprof -- Sampling, systemwide CPU profiler
 * Copyright 2005, Lorenzo Colitti
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

#include "stackstash.h"
#include "module/sysprof-module.h"
#include "profile.h"
#include "process.h"
#include "watch.h"
#include "signal-handler.h"

typedef struct Application Application;
struct Application
{
    int		fd;
    StackStash *stack_stash;
    char *	outfile;
    GMainLoop * main_loop;
};

void
read_trace (StackStash *stash,
	    SysprofStackTrace *trace,
	    GTimeVal now)
{
    Process *process;
    int i;
    
    process = process_get_from_pid (trace->pid);

    for (i = 0; i < trace->n_addresses; ++i)
    {
	process_ensure_map (process, trace->pid, 
			    (gulong)trace->addresses[i]);
    }
    
    stack_stash_add_trace (
	stash, process,
	(gulong *)trace->addresses, trace->n_addresses, 1);
}

void
on_read (gpointer data)
{
    Application *app = data;
    SysprofStackTrace trace;
    int bytesread;
    GTimeVal now;

    bytesread = read (app->fd, &trace, sizeof (trace));
    g_get_current_time (&now);

    if (bytesread < 0)
    {
        perror("read");
        return;
    }

    if (bytesread > 0)
        read_trace (app->stack_stash, &trace, now);
}

void
dump_data (Application *app)
{
    GError *err = NULL;
    Profile *profile = profile_new (app->stack_stash);

    profile_save (profile, app->outfile, &err);

    if (err)
    {
        fprintf (stderr, "%s: %s\n", app->outfile, err->message);
        exit (1);
    }
}

void
signal_handler (int signo, gpointer data)
{
    Application *app = data;
    
    g_print ("signal %d caught: dumping data\n", signo);
    
    dump_data (app);

    g_main_loop_quit (app->main_loop);
}

#define SYSPROF_FILE "/proc/sysprof-trace"

static void
no_module (void)
{
    perror (SYSPROF_FILE);
    fprintf (stderr,
	     "\n"
	     "Can't open /proc/sysprof-trace. You need to insert "
	     "the sysprof kernel module. Run\n"
	     "\n"
	     "       modprobe sysprof-module\n"
	     "\n"
	     "as root.\n");
}

static void
usage (const char *name)
{
    fprintf (stderr,
	     "\n"
	     "Usage: \n"
	     "    %s <outfile>\n"
	     "\n"
	     "On SIGTERM or SIGINT (Ctrl-C) write the profile to <outfile>\n"
	     "\n",
	     name);
}

int
main (int argc,
      char *argv[])
{
    gboolean quit;
    Application *app = g_new0 (Application, 1);
    int fd;
    
    fd = open (SYSPROF_FILE, O_RDONLY);

    quit = FALSE;
    
    if (fd < 0)
    {
	no_module ();
	quit = TRUE;
    }

    if (argc < 2)
    {
	usage (argv[0]);
	quit = TRUE;
    }
    
    if (quit)
	return -1;

    app->fd = fd;
    app->outfile = g_strdup (argv[1]);
    app->stack_stash = stack_stash_new ();
    app->main_loop = g_main_loop_new (NULL, 0);

    signal_set_handler (SIGTERM, signal_handler, app, NULL);
    signal_set_handler (SIGINT, signal_handler, app, NULL);
    
    fd_add_watch (app->fd, app);
    fd_set_read_callback (app->fd, on_read);
    g_main_loop_run (app->main_loop);

    return 0;
}
