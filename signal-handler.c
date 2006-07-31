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
    
    struct sigaction    old_action;
    
    SignalWatch *       next;
};

static int          read_end = -1;
static int          write_end = -1;
static SignalWatch *signal_watches = NULL;

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

static void
remove_signal_watch (SignalWatch *watch)
{
    SignalWatch *prev, *w;
    
    g_return_if_fail (watch != NULL);

    prev = NULL;
    for (w = signal_watches; w != NULL; w = w->next)
    {
        if (w == watch)
        {
            if (prev)
                prev->next = w->next;
            else
                signal_watches = w->next;

            break;
        }
        
        prev = w;
    }
}

static void
signal_handler (int        signo,
                siginfo_t *info,
                void      *data)
{
    /* FIXME: I suppose we should handle short
     * and non-successful writes ...
     *
     * And also, there is a deadlock if so many signals arrive that
     * write() blocks. Then we will be stuck right here, and the
     * main loop will never run. Kinda hard to fix without dropping
     * signals ...
     *
     */
    write (write_end, &signo, sizeof (int));
}

static void
on_read (gpointer data)
{
    SignalWatch *watch;
    int signo;

    /* FIXME: handle short read I suppose */
    read (read_end, &signo, sizeof (int));

    watch = lookup_signal_watch (signo);

    if (watch)
        watch->handler (signo, watch->user_data);
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

    /* FIXME: We should probably make the fd's non-blocking */
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
    
    g_return_val_if_fail (lookup_signal_watch (signo) == NULL, FALSE);

    if (read_end == -1)
    {
        if (!create_pipe (&read_end, &write_end, err))
            return FALSE;

        fd_add_watch (read_end, NULL);
        fd_set_read_callback (read_end, on_read);
    }
    
    watch = g_new0 (SignalWatch, 1);
    
    watch->signo = signo;
    watch->handler = handler;
    watch->user_data = data;
    watch->next = signal_watches;
    signal_watches = watch;
    
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
