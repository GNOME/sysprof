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
    int                 signo;
    SignalFunc          handler;
    gpointer            user_data;
    
    int                 read_end;
    int                 write_end;
    struct sigaction    old_action;
    
    SignalWatch *       next;
};

static SignalWatch *signal_watches;

static SignalWatch *
lookup_signal_watch (int signo)
{
    SignalWatch *w;
    
    for (w = signal_watches; w != NULL; w = w->next)
    {
	if (w->signo == signo)
	    return w;
    }
    
    return NULL;
}

/* These two functions might be interrupted by a signal handler that is
 * going to run lookup_signal_watch(). Assuming that pointer writes are
 * atomic, the code below should be ok.
 */
static void
add_signal_watch (SignalWatch *watch)
{
    g_return_if_fail (lookup_signal_watch (watch->signo) == NULL);
    
    watch->next = signal_watches;
    signal_watches = watch;
}

static void
remove_signal_watch (SignalWatch *watch)
{
    g_return_if_fail (watch != NULL);
    
    /* It's either the first one in the list, or it is the ->next
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
signal_handler (int        signo,
                siginfo_t *info,
                void      *data)
{
    SignalWatch *watch;
    char x = 'x';
    
    watch = lookup_signal_watch (signo);
    write (watch->write_end, &x, 1);
}

static void
on_read (gpointer data)
{
    SignalWatch *watch = data;
    char x;
    
    read (watch->read_end, &x, 1);
    
    /* This is a callback to the application, so things like
     * signal_unset_handler() can be called here.
     */
    watch->handler (watch->signo, watch->user_data);
}

static gboolean
create_pipe (int     *read_end,
	     int     *write_end,
             GError **err)
{
    int p[2];
    
    if (pipe (p) < 0)
    {
        /* FIXME - create an error */
        return FALSE;
    }
    
    if (read_end)
	*read_end = p[0];
    
    if (write_end)
	*write_end = p[1];

    return TRUE;
}

static gboolean
install_signal_handler (int                signo,
                        struct sigaction  *old_action,
                        GError           **err)
{
    struct sigaction action;
    
    memset (&action, 0, sizeof (action));
    
    action.sa_sigaction = signal_handler;
    sigemptyset (&action.sa_mask);
    action.sa_flags = SA_SIGINFO;
    
    if (sigaction (signo, &action, old_action) < 0)
    {
        /* FIXME - create an error */
        return TRUE;
    }

    return TRUE;
}

static void
signal_watch_free (SignalWatch *watch)
{
    fd_remove_watch (watch->read_end);
    
    close (watch->write_end);
    close (watch->read_end);
    
    remove_signal_watch (watch);
    
    g_free (watch);
}

gboolean
signal_set_handler (int          signo,
		    SignalFunc   handler,
		    gpointer     data,
		    GError     **err)
{
    SignalWatch *watch;
    int read_end, write_end;
    
    g_return_val_if_fail (lookup_signal_watch (signo) == NULL, FALSE);
    
    if (!create_pipe (&read_end, &write_end, err))
        return FALSE;
    
    watch = g_new0 (SignalWatch, 1);
    
    watch->signo = signo;
    watch->handler = handler;
    watch->user_data = data;
    watch->read_end = read_end;
    watch->write_end = write_end;
    
    fd_add_watch (watch->read_end, watch);
    fd_set_read_callback (watch->read_end, on_read);
    
    add_signal_watch (watch);
    
    if (!install_signal_handler (signo, &watch->old_action, err))
    {
        signal_watch_free (watch);
        
        return FALSE;
    }
    
    return TRUE;
}

void
signal_unset_handler (int signo)
{
    SignalWatch *watch;
    
    watch = lookup_signal_watch (signo);
    
    g_return_if_fail (watch != NULL);
    
    sigaction (signo, &watch->old_action, NULL);
    
    signal_watch_free (watch);
}
