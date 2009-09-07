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

#include <stdint.h>
#include <glib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <string.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/ioctl.h>

#include "stackstash.h"
#include "collector.h"
#include "module/sysprof-module.h"
#include "watch.h"
#include "process.h"
#include "elfparser.h"

#include "perf_counter.h"
#include "barrier.h"

#define N_PAGES 32		/* Number of pages in the ringbuffer */

typedef struct counter_t counter_t;
typedef struct sample_event_t sample_event_t;
typedef struct mmap_event_t mmap_event_t;
typedef struct comm_event_t comm_event_t;
typedef union counter_event_t counter_event_t;
typedef void (* event_callback_t) (counter_event_t *event, gpointer data);

struct counter_t
{
    int					fd;
    struct perf_counter_mmap_page *	mmap_page;
    uint8_t *				data;
    
    uint64_t				tail;
    int					cpu;

    event_callback_t			callback;
    gpointer				user_data;
    
    GString *				partial;
}; 

struct sample_event_t
{
    struct perf_event_header	header;
    uint64_t			ip;
    uint32_t			pid, tid;
    uint64_t			n_ips;
    uint64_t			ips[1];
};

struct comm_event_t
{
    struct perf_event_header	header;
    uint32_t			pid, tid;
    char			comm[];
};

struct mmap_event_t
{
    struct perf_event_header	header;

    uint32_t			pid, tid;
    uint64_t			addr;
    uint64_t			pgoff;
    char			filename[1];
};

union counter_event_t
{
    struct perf_event_header	header;
    mmap_event_t		mmap;
    comm_event_t		comm;
    sample_event_t		sample;
};

struct Collector
{
    CollectorFunc	callback;
    gpointer		data;
    
    StackStash *	stash;
    GTimeVal		latest_reset;

    int			n_samples;

    GList *		counters;
};

static int
get_n_cpus (void)
{
    return sysconf (_SC_NPROCESSORS_ONLN);
}

static int
sysprof_perf_counter_open (struct perf_counter_attr *attr,
			   pid_t		     pid,
			   int			     cpu,
			   int			     group_fd,
			   unsigned long	     flags)
{
    attr->size = sizeof(*attr);
    
    return syscall (__NR_perf_counter_open, attr, pid, cpu, group_fd, flags);
}

static void
on_read (gpointer data)
{
    uint64_t head, tail;
    counter_t *counter = data;
    int mask = (N_PAGES * process_get_page_size() - 1);
#if 0
    int n_bytes = mask + 1;
    int x;
#endif

    tail = counter->tail;
    
    head = counter->mmap_page->data_head;
    rmb();
    
    if (head < tail)
    {
	g_warning ("sysprof fails at ring buffers\n");
	
	tail = head;
    }
    
#if 0
    /* Verify that the double mapping works */
    x = g_random_int() & mask;
    g_assert (*(counter->data + x) == *(counter->data + x + n_bytes));
#endif
    
    while (head - tail >= sizeof (struct perf_event_header))
    {
	struct perf_event_header *header = (void *)(counter->data + (tail & mask));

	if (header->size > head - tail)
	{
	    /* The kernel did not generate a complete event.
	     * I don't think that can happen, but we may as well
	     * be paranoid.
	     */
	    break;
	}

	counter->callback ((counter_event_t *)header, counter->user_data);

	tail += header->size;
    }

    counter->tail = tail;
    counter->mmap_page->data_tail = tail;
}

/* FIXME: return proper errors */
#define fail(x)								\
    do {								\
	g_printerr ("the fail is strong %s\n", x);			\
	exit (-1);							\
    } while (0)

static void *
map_buffer (counter_t *counter)
{ 
    int n_bytes = N_PAGES * process_get_page_size();
    void *address, *a;

    address = mmap (NULL, n_bytes * 2 + process_get_page_size(), PROT_NONE,
		    MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

    if (address == MAP_FAILED)
	fail ("mmap");

    a = mmap (address + n_bytes, n_bytes + process_get_page_size(),
	      PROT_READ | PROT_WRITE,
	      MAP_SHARED | MAP_FIXED, counter->fd, 0);
    
    if (a != address + n_bytes)
	fail ("mmap");

    a = mmap (address, n_bytes + process_get_page_size(),
	      PROT_READ | PROT_WRITE,
	      MAP_SHARED | MAP_FIXED, counter->fd, 0);

    if (a == MAP_FAILED)
	fail ("mmap");

    if (a != address)
	fail ("mmap");

    return address;
}

static counter_t *
counter_new (int	      cpu,
	     event_callback_t callback,
	     gpointer	      data)
{
    struct perf_counter_attr attr;
    counter_t *counter;
    int fd;
    
    counter = g_new (counter_t, 1);
    
    memset (&attr, 0, sizeof (attr));
    
    attr.type = PERF_TYPE_HARDWARE;
    attr.config = PERF_COUNT_HW_CPU_CYCLES;
    attr.sample_period = 1200000 ;  /* In number of clock cycles -
				     * use frequency instead FIXME
				     */
    attr.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_CALLCHAIN;
    attr.wakeup_events = 100000;
    attr.disabled = TRUE;
    attr.mmap = TRUE;
    attr.comm = TRUE;
    
    fd = sysprof_perf_counter_open (&attr, -1, cpu, -1,  0);
    
    if (fd < 0)
    {
	fail ("perf_counter_open");
	return NULL;
    }

    counter->fd = fd;

    counter->mmap_page = map_buffer (counter);
    
    if (counter->mmap_page == MAP_FAILED)
    {
	fail ("mmap");
	return NULL;
    }
    
    counter->data = (uint8_t *)counter->mmap_page + process_get_page_size ();
    counter->tail = 0;
    counter->cpu = cpu;
    counter->partial = g_string_new (NULL);
    counter->callback = callback;
    counter->user_data = data;
    
    fd_add_watch (fd, counter);
    fd_set_read_callback (fd, on_read);
    
    return counter;
}

static void
counter_enable (counter_t *counter)
{
    ioctl (counter->fd, PERF_COUNTER_IOC_ENABLE);
}

static void
counter_free (counter_t *counter)
{
    munmap (counter->mmap_page, (N_PAGES + 1) * process_get_page_size());
    fd_remove_watch (counter->fd);
    
    close (counter->fd);
    g_string_free (counter->partial, TRUE);
    
    g_free (counter);
}

/*
 * Collector
 */
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

/* callback is called whenever a new sample arrives */
Collector *
collector_new (CollectorFunc callback,
	       gpointer      data)
{
    Collector *collector = g_new0 (Collector, 1);
    
    collector->callback = callback;
    collector->data = data;
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
    Process *process = process_get_from_pid (trace->pid);
    gulong *addrs;
    int i;
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
process_mmap (Collector *collector,
	      mmap_event_t *mmap)
{
    
}

static void
process_comm (Collector *collector,
	      comm_event_t *comm)
{
    
}

static gboolean
is_context (uint64_t addr)
{
    return
	addr == PERF_CONTEXT_HV	||
	addr == PERF_CONTEXT_KERNEL ||
	addr == PERF_CONTEXT_USER ||
	addr == PERF_CONTEXT_GUEST ||
	addr == PERF_CONTEXT_GUEST_KERNEL ||
	addr == PERF_CONTEXT_GUEST_USER;
}

static void
process_sample (Collector *collector,
		sample_event_t *sample)
{
    Process *process = process_get_from_pid (sample->pid);
    gboolean first = collector->n_samples == 0;
    uint64_t context = 0;
    gulong addrs_stack[2048];
    gulong *addrs;
    int n_alloc;
    int i;
    gulong *a;

    n_alloc = sample->n_ips + 2;
    if (n_alloc < 2048)
	addrs = addrs_stack;
    else
	addrs = g_new (gulong, n_alloc);

    a = addrs;
    for (i = 0; i < sample->n_ips; ++i)
    {
	uint64_t addr = sample->ips[i];
	
	if (is_context (addr))
	{
	    /* FIXME: think this through */
	    if (context == PERF_CONTEXT_KERNEL)
		*a++ = 0x01; /* kernel marker */

	    context = addr;
	}
	else
	{
	    if (context == PERF_CONTEXT_KERNEL)
	    {
		if (process_is_kernel_address (addr))
		    *a++ = addr;
	    }
	    else
	    {
		if (!context)
		    g_print ("no context\n");
		
		process_ensure_map (process, sample->pid, addr);
		
		*a++ = addr;
	    }
	}
    }

    *a++ = (gulong)process;
    
    stack_stash_add_trace (collector->stash, addrs, a - addrs, 1);
    
    collector->n_samples++;

    if (collector->callback)
	collector->callback (first, collector->data);

    if (addrs != addrs_stack)
	g_free (addrs);
}

static void
on_event (counter_event_t *	event,
	  gpointer		data)

{
    Collector *collector = data;
    
    switch (event->header.type)
    {
    case PERF_EVENT_MMAP:
	process_mmap (collector, &event->mmap);
	break;
	
    case PERF_EVENT_LOST:
	break;
	
    case PERF_EVENT_COMM:
	process_comm (collector, &event->comm);
	break;
	
    case PERF_EVENT_EXIT:
	break;
	
    case PERF_EVENT_THROTTLE:
	break;
	
    case PERF_EVENT_UNTHROTTLE:
	break;
	
    case PERF_EVENT_FORK:
	break;
	
    case PERF_EVENT_READ:
	break;
	
    case PERF_EVENT_SAMPLE:
	process_sample (collector, &event->sample);
	break;
	
    default:
	g_print ("unknown event: %d (%d)\n",
		 event->header.type, event->header.size);
	break;
    }
}

gboolean
collector_start (Collector  *collector,
		 GError    **err)
{
    int n_cpus = get_n_cpus ();
    GList *list;
    int i;

    for (i = 0; i < n_cpus; ++i)
    {
	counter_t *counter = counter_new (i, on_event, collector);

	collector->counters = g_list_append (collector->counters, counter);
    }
    
    /* Hack to make sure we parse the kernel symbols before
     * starting collection, so the parsing doesn't interfere
     * with the profiling.
     */
    process_is_kernel_address (0);

    for (list = collector->counters; list != NULL; list = list->next)
	counter_enable (list->data);
    
    return TRUE;
}

void
collector_stop (Collector *collector)
{
    GList *list;

    for (list = collector->counters; list != NULL; list = list->next)
    {
	counter_t *counter = list->data;
	
	counter_free (counter);
    }

    g_list_free (collector->counters);
    collector->counters = NULL;
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
    static const char *const everything = "[Everything]";
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

GQuark
collector_error_quark (void)
{
    static GQuark q = 0;
    
    if (q == 0)
        q = g_quark_from_static_string ("collector-error-quark");
    
    return q;
}
