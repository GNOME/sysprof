/* Sysprof -- Sampling, systemwide CPU profiler
 * Copyright 2004, Red Hat, Inc.
 * Copyright 2004, 2005, Soeren Sandmann
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

#include "stackstash.h"
#include "collector.h"
#include "module/sysprof-module.h"
#include "watch.h"
#include "process.h"
#include "elfparser.h"

#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#define SYSPROF_FILE "/dev/sysprof-trace"

static void set_no_module_error (GError **err);
static void set_cant_open_error (GError **err, int eno);

struct Collector
{
    CollectorFunc	callback;
    gpointer		data;
    
    StackStash *	stash;
    int			fd;
    GTimeVal		latest_reset;
    int			n_samples;
    SysprofMmapArea *	map_area;
    unsigned int	current;
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
add_trace_to_stash (const SysprofStackTrace *trace,
		    StackStash              *stash)
{
    int i;
    gulong *addrs;
    Process *process = process_get_from_pid (trace->pid);
    int n_addresses;
    int n_kernel_words;
    int a;
    gulong addrs_stack[2048];
    int n_alloc;

    n_addresses = trace->n_addresses;
    n_kernel_words = trace->n_kernel_words;

    n_alloc = n_addresses + n_kernel_words + 2;
    if (n_alloc <= 2048)
	addrs = addrs_stack;
    else
	addrs = g_new (gulong, n_alloc);
    
    a = 0;
    /* Add kernel addresses */
    if (trace->n_kernel_words)
    {
	for (i = 0; i < trace->n_kernel_words; ++i)
	{
	    gulong addr = (gulong)trace->kernel_stack[i];

	    if (process_is_kernel_address (addr))
		addrs[a++] = addr;
	}

	/* Add kernel marker */
	addrs[a++] = 0x01;
    }

    /* Add user addresses */

    for (i = 0; i < n_addresses; ++i)
    {
	gulong addr = (gulong)trace->addresses[i];
	
	process_ensure_map (process, trace->pid, addr);
	addrs[a++] = addr;
    }

    /* Add process */
    addrs[a++] = (gulong)process;

#if 0
    if (a != n_addresses)
 	g_print ("a: %d, n_addresses: %d, kernel words: %d\n trace->nad %d",
		 a, n_addresses, trace->n_kernel_words, trace->n_addresses);
    
    g_assert (a == n_addresses);
#endif
    
    stack_stash_add_trace (stash, addrs, a, 1);
    
    if (addrs != addrs_stack)
	g_free (addrs);
}

static gboolean
in_dead_period (Collector *collector)
{
    GTimeVal now;
    double diff;

    g_get_current_time (&now);
    
    diff = time_diff (&now, &collector->latest_reset);
    
    if (diff >= 0.0 && diff < RESET_DEAD_PERIOD)
	return TRUE;

    return FALSE;
}



static void
collect_traces (Collector *collector)
{
    gboolean first;
    
    /* After a reset we ignore samples for a short period so that
     * a reset will actually cause 'samples' to become 0
     */
    if (in_dead_period (collector))
    {
	collector->current = collector->map_area->head;
	return;
    }

    first = collector->n_samples == 0;
    
    while (collector->current != collector->map_area->head)
    {
	const SysprofStackTrace *trace;

	trace = &(collector->map_area->traces[collector->current]);

#if 0
	{
	    int i;
	    g_print ("pid: %d (%d)\n", trace->pid, trace->n_addresses);
	    for (i=0; i < trace->n_addresses; ++i)
		g_print ("rd: %08x\n", trace->addresses[i]);
	    g_print ("-=-\n");
	}
#endif
#if 0
	{
	    int i;
	    g_print ("pid: %d (%d)\n", trace->pid, trace->n_addresses);
	    for (i=0; i < trace->n_kernel_words; ++i)
		g_print ("rd: %08x\n", trace->kernel_stack[i]);
	    g_print ("-=-\n");
	}
#endif
	
	add_trace_to_stash (trace, collector->stash);
	
	collector->current++;
	if (collector->current >= SYSPROF_N_TRACES)
	    collector->current = 0;
	
	collector->n_samples++;
    }
	
    if (collector->callback)
	collector->callback (first, collector->data);
}

static void
on_read (gpointer data)
{
    Collector *collector = data;
    char c;
    
    /* Make sure poll() doesn't fire immediately again */
    read (collector->fd, &c, 1);

    collect_traces (collector);
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
    void *map_area;
    
    fd = open (SYSPROF_FILE, O_RDONLY);
    if (fd < 0)
    {
	if (load_module())
	{
	    GTimer *timer = g_timer_new ();
	    
	    while (fd < 0 && g_timer_elapsed (timer, NULL) < 0.5)
	    {
		/* Wait for udev to discover the new device.
		 */
		usleep (100000);
		
		errno = 0;
		fd = open (SYSPROF_FILE, O_RDONLY);
	    }
	    
	    g_timer_destroy (timer);
	    
	    if (fd < 0)
	    {
		set_cant_open_error (err, errno);
		return FALSE;
	    }
	}
	
	if (fd < 0)
	{
	    set_no_module_error (err);
	    
	    return FALSE;
	}
    }
    
    map_area = mmap (NULL, sizeof (SysprofMmapArea),
		     PROT_READ, MAP_SHARED, fd, 0);
    
    if (map_area == MAP_FAILED)
    {
	close (fd);
	set_cant_open_error (err, errno);
	
	return FALSE;
    }
    
    collector->map_area = map_area;
    collector->current = 0;
    collector->fd = fd;
    fd_add_watch (collector->fd, collector);
    
    return TRUE;
}

gboolean
collector_start (Collector  *collector,
		 GError    **err)
{
    if (collector->fd < 0 && !open_fd (collector, err))
	return FALSE;
    
    /* Hack to make sure we parse the kernel symbols before
     * starting collection, so the parsing doesn't interfere
     * with the profiling.
     */
    process_is_kernel_address (0);
    
    fd_set_read_callback (collector->fd, on_read);
    return TRUE;
}

void
collector_stop (Collector *collector)
{
    if (collector->fd >= 0)
    {
	fd_remove_watch (collector->fd);
	
	munmap (collector->map_area, sizeof (SysprofMmapArea));
	collector->map_area = NULL;
	collector->current = 0;
	
	close (collector->fd);
	collector->fd = -1;
    }
}

void
collector_reset (Collector *collector)
{
    if (collector->stash)
	stack_stash_unref (collector->stash);
    
    process_flush_caches();
    
    collector->stash = stack_stash_new (NULL);
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
    GHashTable *unique_cmdlines;
} ResolveInfo;

/* Note that 'unique_symbols' is a direct_hash table. Ie., we
 * rely on the address of symbol strings being different for different
 * symbols.
 */
static char *
unique_dup (GHashTable *unique_symbols, const char *sym)
{
    char *result;
    
    result = g_hash_table_lookup (unique_symbols, sym);
    if (!result)
    {
	result = elf_demangle (sym);
	g_hash_table_insert (unique_symbols, (char *)sym, result);
    }
    
    return result;
}

static char *
lookup_symbol (Process *process, gpointer address,
	       GHashTable *unique_symbols,
	       gboolean kernel,
	       gboolean first_addr)
{
    const char *sym;

    g_assert (process);

    if (kernel)
    {
	gulong offset;
	sym = process_lookup_kernel_symbol ((gulong)address, &offset);

	/* If offset is 0, it is a callback, not a return address.
	 *
	 * If "first_addr" is true, then the address is an
	 * instruction pointer, not a return address, so it may
	 * legitimately be at offset 0.
	 */
	if (offset == 0 && !first_addr)
	{
#if 0
	    g_print ("rejecting callback: %s (%p)\n", sym, address);
#endif
	    sym = NULL;
	}

	/* If offset is greater than 4096, then what happened is most
	 * likely that it is the address of something in the gap between the
	 * kernel text and the text of the modules. Rather than assign
	 * this to the last function of the kernel text, we remove it here.
	 *
	 * FIXME: what we really should do is find out where this split
	 * is, and act accordingly. Actually, we should look at /proc/modules
	 */
	if (offset > 4096)
	{
#if 0
	    g_print ("offset\n");
#endif
	    sym = NULL;
	}
    }
    else
    {
	gulong offset;
	
	sym = process_lookup_symbol (process, (gulong)address, &offset);

	if (offset == 0 && !first_addr)
	{
#if 0
	    sym = g_strdup_printf ("%s [callback]", sym);
	    g_print ("rejecting %s since it looks like a callback\n",
		     sym);
	    sym = NULL;
#endif
	}
    }
    
    if (sym)
	return unique_dup (unique_symbols, sym);
    else
	return NULL;
}

static void
resolve_symbols (GList *trace, gint size, gpointer data)
{
    static const char *const everything = "Everything";
    GList *list;
    ResolveInfo *info = data;
    Process *process = g_list_last (trace)->data;
    GPtrArray *resolved_trace = g_ptr_array_new ();
    char *cmdline;
    gboolean in_kernel = FALSE;
    gboolean first_addr = TRUE;
    
    for (list = trace; list && list->next; list = list->next)
    {
	if (list->data == GINT_TO_POINTER (0x01))
	    in_kernel = TRUE;
    }

    for (list = trace; list && list->next; list = list->next)
    {
	gpointer address = list->data;
	char *symbol;
	
	if (address == GINT_TO_POINTER (0x01))
	    in_kernel = FALSE;
	
	symbol = lookup_symbol (process, address, info->unique_symbols,
				in_kernel, first_addr);
	first_addr = FALSE;

	if (symbol)
	    g_ptr_array_add (resolved_trace, symbol);
    }

    cmdline = g_hash_table_lookup (info->unique_cmdlines,
				   (char *)process_get_cmdline (process));
    if (!cmdline)
    {
	cmdline = g_strdup (process_get_cmdline (process));

	g_hash_table_insert (info->unique_cmdlines, cmdline, cmdline);
    }
    
    g_ptr_array_add (resolved_trace, cmdline);
    
    g_ptr_array_add (resolved_trace,
		     unique_dup (info->unique_symbols, everything));
    
    stack_stash_add_trace (info->resolved_stash,
			   (gulong *)resolved_trace->pdata,
			   resolved_trace->len, size);
    
    g_ptr_array_free (resolved_trace, TRUE);
}

Profile *
collector_create_profile (Collector *collector)
{
    ResolveInfo info;
    Profile *profile;

    info.resolved_stash = stack_stash_new ((GDestroyNotify)g_free);
    info.unique_symbols = g_hash_table_new (g_direct_hash, g_direct_equal);
    info.unique_cmdlines = g_hash_table_new (g_str_hash, g_str_equal);
    
    stack_stash_foreach (collector->stash, resolve_symbols, &info);

    g_hash_table_destroy (info.unique_symbols);
    g_hash_table_destroy (info.unique_cmdlines);
    
    profile = profile_new (info.resolved_stash);
    
    stack_stash_unref (info.resolved_stash);

    return profile;
}

static void
set_no_module_error (GError **err)
{
    g_set_error (err,
		 COLLECTOR_ERROR,
		 COLLECTOR_ERROR_CANT_OPEN_FILE,
		 "Can't open " SYSPROF_FILE ". You need to insert "
		 "the sysprof kernel module. Run\n"
		 "\n"
		 "       modprobe sysprof-module\n"
		 "\n"
		 "as root");
}

static void
set_cant_open_error (GError **err,
		     int      eno)
{
    g_set_error (err,
		 COLLECTOR_ERROR,
		 COLLECTOR_ERROR_CANT_OPEN_FILE,
		 "Can't open " SYSPROF_FILE ": %s",
		 g_strerror (eno));
}

GQuark
collector_error_quark (void)
{
    static GQuark q = 0;
    
    if (q == 0)
        q = g_quark_from_static_string ("collector-error-quark");
    
    return q;
}
