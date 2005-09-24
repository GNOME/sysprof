/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*- */

/*  Sysprof -- Sampling, systemwide CPU profiler
 *  Copyright (C) 2005  Søren Sandmann (sandmann@daimi.au.dk)
 *
 *  This library is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <signal.h>
#include <unistd.h>
#include <string.h>
#include "watch.h"
#include "signal-handler.h"

typedef struct SignalWatch SignalWatch;
struct SignalWatch
{
    int signo;
    int pipe_read_end;
    int pipe_write_end;
    struct sigaction old_action;
    SignalFunc handler;
    gpointer user_data;

    SignalWatch *next;
};

/* This is not a GHashTable because I don't trust g_hash_table_lookup()
 * to be callable from a signal handler.
 */
static SignalWatch *signal_watches;

static SignalWatch *
lookup_watch (int signo)
{
    SignalWatch *w;

    for (w = signal_watches; w != NULL; w = w->next)
    {
	if (w->signo == signo)
	    return w;
    }

    return NULL;
}

static void
add_watch (SignalWatch *watch)
{
    g_return_if_fail (lookup_watch (watch->signo) == NULL);
    
    watch->next = signal_watches;
    signal_watches = watch;
}

static void
remove_watch (SignalWatch *watch)
{
    /* it's either the first one in the list, or it is the ->next
     * for some other watch
     */
    if (watch == signal_watches)
    {
	signal_watches = watch->next;
	watch->next = NULL;
    }
    else
    {
	SignalWatch *w;
	
	for (w = signal_watches; w && w->next; w = w->next)
	{
	    if (watch == w->next)
	    {
		w->next = w->next->next;
		watch->next = NULL;
		break;
	    }
	}
    }
}

static void
signal_handler (int signo, siginfo_t *info, void *data)
{
    SignalWatch *watch;
    char x = 'x';

    watch = lookup_watch (signo);
    write (watch->pipe_write_end, &x, 1);
}

static void
on_read (gpointer data)
{
    SignalWatch *watch = data;
    char x;
    
    read (watch->pipe_read_end, &x, 1);
    
    watch->handler (watch->signo, watch->user_data);
}

static void
create_pipe (int *read_end,
	     int *write_end)
{
    int p[2];

    pipe (p);

    if (read_end)
	*read_end = p[0];

    if (write_end)
	*write_end = p[1];
}

static void
install_signal_handler (int signo, struct sigaction *old_action)
{
    struct sigaction action;

    memset (&action, 0, sizeof (action));

    action.sa_sigaction = signal_handler;
    action.sa_flags = SA_SIGINFO;

    sigaction (signo, &action, old_action);
}

gboolean
signal_set_handler (int signo,
		    SignalFunc handler,
		    gpointer data,
		    GError **err)
{
    SignalWatch *watch;
    
    g_return_val_if_fail (lookup_watch (signo) == NULL, FALSE);
    
    watch = g_new0 (SignalWatch, 1);

    create_pipe (&watch->pipe_read_end, &watch->pipe_write_end);
    watch->signo = signo;
    watch->handler = handler;
    watch->user_data = data;

    add_watch (watch);
	
    fd_add_watch (watch->pipe_read_end, watch);
    fd_set_read_callback (watch->pipe_read_end, on_read);

    install_signal_handler (signo, &watch->old_action);

    return TRUE;
}

void
signal_unset_handler (int signo)
{
    SignalWatch *watch;
    
    watch = lookup_watch (signo);

    g_return_if_fail (watch != NULL);

    sigaction (signo, &watch->old_action, NULL);

    fd_remove_watch (watch->pipe_read_end);
    
    close (watch->pipe_read_end);
    close (watch->pipe_write_end);

    remove_watch (watch);
    
    g_free (watch);
}
