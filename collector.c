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
#include "elfparser.h"
#include "tracker.h"

#include "perf_counter.h"
#include "barrier.h"

#define N_PAGES 32		/* Number of pages in the ringbuffer */

typedef struct counter_t counter_t;
typedef struct sample_event_t sample_event_t;
typedef struct mmap_event_t mmap_event_t;
typedef struct comm_event_t comm_event_t;
typedef struct exit_event_t exit_event_t;
typedef struct fork_event_t fork_event_t;
typedef union counter_event_t counter_event_t;

static void process_event (Collector *collector, counter_event_t *event);

struct counter_t
{
    Collector *				collector;
    
    int					fd;
    struct perf_counter_mmap_page *	mmap_page;
    uint8_t *				data;
    
    uint64_t				tail;
    int					cpu;
    
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
    char			comm[1];
};

struct mmap_event_t
{
    struct perf_event_header	header;

    uint32_t			pid, tid;
    uint64_t			addr;
    uint64_t			len;
    uint64_t			pgoff;
    char			filename[1];
};

struct fork_event_t
{
    struct perf_event_header	header;
    
    uint32_t			pid, ppid;
    uint32_t			tid, ptid;
};

struct exit_event_t
{
    struct perf_event_header	header;

    uint32_t			pid, ppid;
    uint32_t			tid, ptid;
};

union counter_event_t
{
    struct perf_event_header	header;
    mmap_event_t		mmap;
    comm_event_t		comm;
    sample_event_t		sample;
    fork_event_t		fork;
    exit_event_t		exit;
};

struct Collector
{
    CollectorFunc	callback;
    gpointer		data;
    
    tracker_t *		tracker;
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

static int
get_page_size (void)
{
    static int page_size;
    static gboolean has_page_size = FALSE;

    if (!has_page_size)
    {
	page_size = getpagesize();
	has_page_size = TRUE;
    }

    return page_size;
}

static void
on_read (gpointer data)
{
    counter_t *counter = data;
    int mask = (N_PAGES * get_page_size() - 1);
    gboolean skip_samples;
    Collector *collector;
    uint64_t head, tail;
    gboolean first;

    collector = counter->collector;
    first = collector->n_samples == 0;
    
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

    skip_samples = in_dead_period (collector);
    
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

	if (!skip_samples || header->type != PERF_EVENT_SAMPLE)
	{
	    if (header->type == PERF_EVENT_SAMPLE)
		collector->n_samples++;
	    
	    process_event (collector, (counter_event_t *)header);
	}
	
	tail += header->size;
    }

    counter->tail = tail;
    counter->mmap_page->data_tail = tail;

    if (collector->callback)
	collector->callback (first, collector->data);
}

/* FIXME: return proper errors */
#define fail(x)								\
    do {								\
	g_printerr ("the fail is ");					\
	perror (x);							\
	exit (-1);							\
    } while (0)

static void *
map_buffer (counter_t *counter)
{ 
    int n_bytes = N_PAGES * get_page_size();
    void *address, *a;

    /* We use the old trick of mapping the ring buffer twice
     * consecutively, so that we don't need special-case code
     * to deal with wrapping.
     */
    address = mmap (NULL, n_bytes * 2 + get_page_size(), PROT_NONE,
		    MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

    if (address == MAP_FAILED)
	fail ("mmap");

    a = mmap (address + n_bytes, n_bytes + get_page_size(),
	      PROT_READ | PROT_WRITE,
	      MAP_SHARED | MAP_FIXED, counter->fd, 0);
    
    if (a != address + n_bytes)
	fail ("mmap");

    a = mmap (address, n_bytes + get_page_size(),
	      PROT_READ | PROT_WRITE,
	      MAP_SHARED | MAP_FIXED, counter->fd, 0);

    if (a == MAP_FAILED)
	fail ("mmap");

    if (a != address)
	fail ("mmap");

    return address;
}

static counter_t *
counter_new (Collector *collector,
	     int	cpu)
{
    struct perf_counter_attr attr;
    counter_t *counter;
    int fd;
    
    counter = g_new (counter_t, 1);
    
    memset (&attr, 0, sizeof (attr));
    
    attr.type = PERF_TYPE_HARDWARE;
    attr.config = PERF_COUNT_HW_CPU_CYCLES;
    attr.sample_period = 1200000 ;  /* In number of clock cycles -
				     * FIXME: consider using frequency instead
				     */
    attr.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_CALLCHAIN;
    attr.wakeup_events = 100000;
    attr.disabled = TRUE;

    if (cpu == 0)
    {
	attr.mmap = 1;
	attr.comm = 1;
	attr.task = 1;
    }
    
    fd = sysprof_perf_counter_open (&attr, -1, cpu, -1,  0);
    
    if (fd < 0)
    {
	fail ("perf_counter_open");
	return NULL;
    }

    counter->collector = collector;
    counter->fd = fd;

    counter->mmap_page = map_buffer (counter);
    
    if (counter->mmap_page == MAP_FAILED)
    {
	fail ("mmap");
	return NULL;
    }
    
    counter->data = (uint8_t *)counter->mmap_page + get_page_size ();
    counter->tail = 0;
    counter->cpu = cpu;
    counter->partial = g_string_new (NULL);
    
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
    munmap (counter->mmap_page, (N_PAGES + 1) * get_page_size());
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
    gboolean restart = FALSE;
    
    if (collector->counters)
    {
	collector_stop (collector);
	restart = TRUE;
    }
    
    if (collector->tracker)
    {
	tracker_free (collector->tracker);
	collector->tracker = NULL;
    }

    collector->n_samples = 0;
    
    g_get_current_time (&collector->latest_reset);

    if (restart)
	collector_start (collector, NULL);
}

/* callback is called whenever a new sample arrives */
Collector *
collector_new (CollectorFunc callback,
	       gpointer      data)
{
    Collector *collector = g_new0 (Collector, 1);
    
    collector->callback = callback;
    collector->data = data;
    collector->tracker = NULL;
    
    collector_reset (collector);

    return collector;
}

static void
process_mmap (Collector *collector, mmap_event_t *mmap)
{
    tracker_add_map (collector->tracker,
		     mmap->pid,
		     mmap->addr,
		     mmap->addr + mmap->len,
		     mmap->pgoff,
		     0, /* inode */
		     mmap->filename);
}

static void
process_comm (Collector *collector, comm_event_t *comm)
{
    g_print ("comm: pid: %d\n", comm->pid);
    
    tracker_add_process (collector->tracker,
			 comm->pid,
			 comm->comm);
}

static void
process_fork (Collector *collector, fork_event_t *fork)
{
    g_print ("fork: ppid: %d  pid: %d\n", fork->ppid, fork->pid);
    
    tracker_add_fork (collector->tracker, fork->ppid, fork->pid);
}

static void
process_exit (Collector *collector, exit_event_t *exit)
{
    tracker_add_exit (collector->tracker, exit->pid);
}

static void
process_sample (Collector      *collector,
		sample_event_t *sample)
{
    uint64_t *ips;
    int n_ips;
    if (sample->n_ips == 0)
    {
	ips = &sample->ip;
	n_ips = 1;
    }
    else
    {
	ips = sample->ips;
	n_ips = sample->n_ips;
    }

    tracker_add_sample (collector->tracker,
			sample->pid, ips, n_ips);
}

static void
process_event (Collector       *collector,
	       counter_event_t *event)
{
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
	process_exit (collector, &event->exit);
	break;
	
    case PERF_EVENT_THROTTLE:
	break;
	
    case PERF_EVENT_UNTHROTTLE:
	break;
	
    case PERF_EVENT_FORK:
	process_fork (collector, &event->fork);
	break;
	
    case PERF_EVENT_READ:
	break;
	
    case PERF_EVENT_SAMPLE:
	process_sample (collector, &event->sample);
	break;
	
    default:
	g_warning ("Got unknown event: %d (%d)\n",
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

    if (!collector->tracker)
	collector->tracker = tracker_new ();
    
    for (i = 0; i < n_cpus; ++i)
    {
	counter_t *counter = counter_new (collector, i);

	collector->counters = g_list_append (collector->counters, counter);
    }
    
    /* Hack to make sure we parse the kernel symbols before
     * starting collection, so the parsing doesn't interfere
     * with the profiling.
     */

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

Profile *
collector_create_profile (Collector *collector)
{
    return tracker_create_profile (collector->tracker);
}

GQuark
collector_error_quark (void)
{
    static GQuark q = 0;
    
    if (q == 0)
        q = g_quark_from_static_string ("collector-error-quark");
    
    return q;
}
