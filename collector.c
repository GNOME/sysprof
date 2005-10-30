#include "stackstash.h"
#include "collector.h"
#include "module/sysprof-module.h"
#include "watch.h"
#include "process.h"

#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

struct Collector
{
    CollectorFunc	callback;
    gpointer		data;
    
    StackStash *	stash;
    int			fd;
    GTimeVal		latest_reset;
    int			n_samples;
};

/* callback is called whenever a new sample arrives */
Collector *
collector_new (CollectorFunc callback,
	      gpointer     data)
{
    Collector *collector = g_new0 (Collector, 1);
    
    collector->callback = callback;
    collector->data = data;
    collector->fd = -1;
    collector->stash = NULL;
    
    collector_reset (collector);
    
    return collector;
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
    Collector *collector = data;
    GTimeVal now;
    int rd;
    
    rd = read (collector->fd, &trace, sizeof (trace));
    
    if (rd == -1 && errno == EWOULDBLOCK)
	return;
    
    g_get_current_time (&now);
    
    /* After a reset we ignore samples for a short period so that
     * a reset will actually cause 'samples' to become 0
     */
    if (time_diff (&now, &collector->latest_reset) < RESET_DEAD_PERIOD)
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
	add_trace_to_stash (&trace, collector->stash);
	
	collector->n_samples++;
    }
    
    if (collector->callback)
	collector->callback (collector->data);
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
open_fd (Collector *collector,
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
    
    collector->fd = fd;
    fd_add_watch (collector->fd, collector);
    
    return TRUE;
}

static void
empty_file_descriptor (Collector *collector)
{
    int rd;
    SysprofStackTrace trace;
    
    do
    {
	rd = read (collector->fd, &trace, sizeof (trace));
	
    } while (rd != -1); /* until EWOULDBLOCK */
}

gboolean
collector_start (Collector *collector,
		GError **err)
{
    if (collector->fd < 0 && !open_fd (collector, err))
	return FALSE;
    
    fd_set_read_callback (collector->fd, on_read);
    return TRUE;
}

void
collector_stop (Collector *collector)
{
    fd_set_read_callback (collector->fd, NULL);
}

void
collector_reset (Collector *collector)
{
    if (collector->stash)
	stack_stash_free (collector->stash);
    
    collector->stash = stack_stash_new ();
    collector->n_samples = 0;
    
    g_get_current_time (&collector->latest_reset);
}

int
collector_get_n_samples (Collector *collector)
{
    return collector->n_samples;
}

typedef struct
{
    StackStash *resolved_stash;
    GHashTable *unique_symbols;
} ResolveInfo;

static char *
unique_dup (GHashTable *unique_symbols, char *s)
{
    char *result;
    
    result = g_hash_table_lookup (unique_symbols, s);
    if (!result)
    {
	result = g_strdup (s);
	g_hash_table_insert (unique_symbols, s, result);
    }
    
    return result;
}

static char *
lookup_symbol (Process *process, gpointer address, GHashTable *unique_symbols)
{
    const Symbol *s = process_lookup_symbol (process, (gulong)address);
    
    return unique_dup (unique_symbols, s->name);
}

static void
resolve_symbols (GSList *trace, gint size, gpointer data)
{
    GSList *slist;
    ResolveInfo *info = data;
    Process *process = g_slist_last (trace)->data;
    GPtrArray *resolved_trace = g_ptr_array_new ();
    
    for (slist = trace; slist && slist->next; slist = slist->next)
    {
	gpointer address = slist->data;
	char *symbol;
	
	symbol = lookup_symbol (process, address, info->unique_symbols);
	
	g_ptr_array_add (resolved_trace, symbol);
    }
    
    g_ptr_array_add (resolved_trace,
		     unique_dup (info->unique_symbols,
				 (char *)process_get_cmdline (process)));
    g_ptr_array_add (resolved_trace,
		     unique_dup (info->unique_symbols,
				 "Everything"));
    
    stack_stash_add_trace (info->resolved_stash, (gulong *)resolved_trace->pdata, resolved_trace->len, size);
}

Profile *
collector_create_profile (Collector *collector)
{
    ResolveInfo info;
    
    info.resolved_stash = stack_stash_new ();
    info.unique_symbols = g_hash_table_new (g_direct_hash, g_direct_equal);
    
    stack_stash_foreach (collector->stash, resolve_symbols, &info);
    
    g_hash_table_destroy (info.unique_symbols);
    
    return profile_new (info.resolved_stash);
}
