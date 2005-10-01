#include "stackstash.h"
#include "profiler.h"
#include "module/sysprof-module.h"
#include "watch.h"

#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

struct Profiler
{
    ProfilerFunc	callback;
    gpointer		data;
    
    StackStash *	stash;
    int			fd;
    GTimeVal		latest_reset;
    int			n_samples;
};

/* callback is called whenever a new sample arrives */
Profiler *
profiler_new (ProfilerFunc callback,
	      gpointer     data)
{
    Profiler *profiler = g_new0 (Profiler, 1);
    
    profiler->callback = callback;
    profiler->data = data;
    profiler->fd = -1;
    profiler->stash = NULL;
    
    profiler_reset (profiler);

    return profiler;
}

static double
timeval_to_ms (const GTimeVal *timeval)
{
    return (timeval->tv_sec * G_USEC_PER_SEC + timeval->tv_usec) / 1000.0;
}

static double
time_diff (const GTimeVal *first,
	   const GTimeVal *second)
{
    double first_ms = timeval_to_ms (first);
    double second_ms = timeval_to_ms (second);
    
    return first_ms - second_ms;
}

#define RESET_DEAD_PERIOD 250

static void
add_trace_to_stash (SysprofStackTrace *trace,
		    StackStash *stash)
{
    int i;
    gulong *addrs;
    Process *process = process_get_from_pid (trace->pid);
    
    addrs = g_new (gulong, trace->n_addresses + 1);

    for (i = 0; i < trace->n_addresses; ++i)
    {
	process_ensure_map (process, trace->pid, 
			    (gulong)trace->addresses[i]);

	addrs[i] = (gulong)trace->addresses[i];
    }

    addrs[i] = (gulong)process;
    
    stack_stash_add_trace (
	stash, addrs, trace->n_addresses + 1, 1);
    
    g_free (addrs);
}

static void
on_read (gpointer data)
{
    SysprofStackTrace trace;
    Profiler *profiler = data;
    GTimeVal now;
    int rd;
    
    rd = read (profiler->fd, &trace, sizeof (trace));
    
    if (rd == -1 && errno == EWOULDBLOCK)
	return;
    
    g_get_current_time (&now);
    
    /* After a reset we ignore samples for a short period so that
     * a reset will actually cause 'samples' to become 0
     */
    if (time_diff (&now, &profiler->latest_reset) < RESET_DEAD_PERIOD)
	return;
    
#if 0
    int i;
    g_print ("pid: %d\n", trace.pid);
    for (i=0; i < trace.n_addresses; ++i)
	g_print ("rd: %08x\n", trace.addresses[i]);
    g_print ("-=-\n");
#endif
    
    if (rd > 0)
    {
	add_trace_to_stash (&trace, profiler->stash);
	
	profiler->n_samples++;
    }

    if (profiler->callback)
	profiler->callback (profiler->data);
}

static gboolean
load_module (void)
{
    int exit_status = -1;
    char *dummy1, *dummy2;
    
    if (g_spawn_command_line_sync ("/sbin/modprobe sysprof-module",
				   &dummy1, &dummy2,
				   &exit_status,
				   NULL))
    {
	if (WIFEXITED (exit_status))
	    exit_status = WEXITSTATUS (exit_status);
	
	g_free (dummy1);
	g_free (dummy2);
    }
    
    return (exit_status == 0);
}

static gboolean
open_fd (Profiler *profiler,
	   GError **err)
{
    int fd;
    
    fd = open ("/proc/sysprof-trace", O_RDONLY);
    if (fd < 0)
    {
	load_module();
	
	fd = open ("/proc/sysprof-trace", O_RDONLY);
	
	if (fd < 0)
	{
	    /* FIXME: set error */
#if 0
	    sorry (app->main_window,
		   "Can't open /proc/sysprof-trace. You need to insert\n"
		   "the sysprof kernel module. Run\n"
		   "\n"
		   "       modprobe sysprof-module\n"
		   "\n"
		   "as root.");
#endif
	    
	    return FALSE;
	}
    }
    
    profiler->fd = fd;
    fd_add_watch (profiler->fd, profiler);

    return TRUE;
}

static void
empty_file_descriptor (Profiler *profiler)
{
    int rd;
    SysprofStackTrace trace;
    
    do
    {
	rd = read (profiler->fd, &trace, sizeof (trace));
	
    } while (rd != -1); /* until EWOULDBLOCK */
}

gboolean
profiler_start (Profiler *profiler,
		GError **err)
{
    if (profiler->fd < 0 && !open_fd (profiler, err))
	return FALSE;
    
    fd_set_read_callback (profiler->fd, on_read);
    return TRUE;
}

void
profiler_stop (Profiler *profiler)
{
    fd_set_read_callback (profiler->fd, NULL);
}

void
profiler_reset (Profiler *profiler)
{
    if (profiler->stash)
	stack_stash_free (profiler->stash);
    
    profiler->stash = stack_stash_new ();
    profiler->n_samples = 0;
    
    g_get_current_time (&profiler->latest_reset);
}

int
profiler_get_n_samples (Profiler *profiler)
{
    return profiler->n_samples;
}

Profile *
profiler_create_profile (Profiler *profiler)
{
    return profile_new (profiler->stash);
}
