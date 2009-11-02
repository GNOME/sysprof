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
#include <errno.h>

#include "stackstash.h"
#include "collector.h"
#include "watch.h"
#include "elfparser.h"
#include "tracker.h"

#include "perf_counter.h"
#include "barrier.h"

#define d_print(...)

#define N_PAGES 32		/* Number of pages in the ringbuffer */

#define N_WAKEUP_EVENTS		149

typedef struct counter_t counter_t;
typedef struct sample_event_t sample_event_t;
typedef struct mmap_event_t mmap_event_t;
typedef struct comm_event_t comm_event_t;
typedef struct exit_event_t exit_event_t;
typedef struct fork_event_t fork_event_t;
typedef union counter_event_t counter_event_t;

static void process_event (Collector *collector,
			   counter_t *counter,
			   counter_event_t *event);

struct counter_t
{
    Collector *				collector;
    
    int					fd;
    struct perf_counter_mmap_page *	mmap_page;
    uint8_t *				data;
    
    uint64_t				tail;
    int					cpu;
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

    int			prev_samples;
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
#ifndef __NR_perf_counter_open
#define __NR_perf_counter_open 336
#endif
    
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

    collector = counter->collector;
    
#if 0
    int n_bytes = mask + 1;
    int x;
#endif

    tail = counter->tail;
    
    head = counter->mmap_page->data_head;
    rmb();
    
    if (head < tail)
    {
	g_warning ("sysprof fails at ring buffers (head %llu, tail %llu\n", head, tail);
	
	tail = head;
    }
    
#if 0
    /* Verify that the double mapping works */
    x = g_random_int() & mask;
    g_assert (*(counter->data + x) == *(counter->data + x + n_bytes));
#endif

    skip_samples = in_dead_period (collector);

#if 0
    g_print ("n bytes %d\n", head - tail);
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

	if (!skip_samples || header->type != PERF_EVENT_SAMPLE)
	{
	    if (header->type == PERF_EVENT_SAMPLE)
		collector->n_samples++;
	    
	    process_event (collector, counter, (counter_event_t *)header);
	}
	
	tail += header->size;
    }

    counter->tail = tail;
    counter->mmap_page->data_tail = tail;

    if (collector->callback)
    {
	if (collector->n_samples - collector->prev_samples >= N_WAKEUP_EVENTS)
	{
	    gboolean first_sample = collector->prev_samples == 0;
	    
	    collector->callback (first_sample, collector->data);

	    collector->prev_samples = collector->n_samples;
	}
    }
}

static void *
fail (GError **err, const char *what)
{
    g_set_error (err, COLLECTOR_ERROR, COLLECTOR_ERROR_FAILED,
		 "%s: %s", what, strerror (errno));

    return NULL;
}
	
static void *
map_buffer (counter_t *counter, GError **err)
{ 
    int n_bytes = N_PAGES * get_page_size();
    void *address, *a;

    /* We map the ring buffer twice in consecutive address space,
     * so that we don't need special-case code to deal with wrapping.
     */
    address = mmap (NULL, n_bytes * 2 + get_page_size(), PROT_NONE,
		    MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

    if (address == MAP_FAILED)
	return fail (err, "mmap");
    
    a = mmap (address + n_bytes, n_bytes + get_page_size(),
	      PROT_READ | PROT_WRITE,
	      MAP_SHARED | MAP_FIXED, counter->fd, 0);
    
    if (a != address + n_bytes)
	return fail (err, "mmap");

    a = mmap (address, n_bytes + get_page_size(),
	      PROT_READ | PROT_WRITE,
	      MAP_SHARED | MAP_FIXED, counter->fd, 0);

    if (a == MAP_FAILED || a != address)
	return fail (err, "mmap");

    if (a != address)
	return fail (err, "mmap");

    return address;
}

static counter_t *
counter_new (Collector  *collector,
	     int	 cpu,
	     GError    **err)
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
    attr.wakeup_events = N_WAKEUP_EVENTS;
    attr.disabled = TRUE;

    attr.mmap = 1;
    attr.comm = 1;
    attr.task = 1;
    attr.exclude_idle = 1;
    
    if ((fd = sysprof_perf_counter_open (&attr, -1, cpu, -1,  0)) < 0)
    {
	if (errno == ENODEV)
	{
	    attr.type = PERF_TYPE_SOFTWARE;
	    attr.config = PERF_COUNT_SW_CPU_CLOCK;
	    attr.sample_period = 1000000;

	    fd = sysprof_perf_counter_open (&attr, -1, cpu, -1, 0);
	}
    }
    
    if (fd < 0)
	return fail (err, "Could not open performance counter");

    counter->collector = collector;
    counter->fd = fd;

    counter->mmap_page = map_buffer (counter, err);
    
    if (!counter->mmap_page || counter->mmap_page == MAP_FAILED)
	return NULL;
    
    counter->data = (uint8_t *)counter->mmap_page + get_page_size ();
    counter->tail = 0;
    counter->cpu = cpu;
    
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
counter_disable (counter_t *counter)
{
    d_print ("disable\n");
    
    ioctl (counter->fd, PERF_COUNTER_IOC_DISABLE);
}

static void
counter_free (counter_t *counter)
{
    d_print ("munmap\n");

    munmap (counter->mmap_page, (N_PAGES + 1) * get_page_size());
    fd_remove_watch (counter->fd);
    
    close (counter->fd);
    
    g_free (counter);
}

/*
 * Collector
 */
static void
enable_counters (Collector *collector)
{
    GList *list;

    d_print ("enable\n");
    
    for (list = collector->counters; list != NULL; list = list->next)
    {
	counter_t *counter = list->data;

	counter_enable (counter);
    }
}

static void
disable_counters (Collector *collector)
{
    GList *list;

    d_print ("disable\n");
    
    for (list = collector->counters; list != NULL; list = list->next)
    {
	counter_t *counter = list->data;
	
	counter_disable (counter);
    }
}

void
collector_reset (Collector *collector)
{
    /* Disable the counters so that we won't track
     * the activity of tracker_free()/tracker_new()
     *
     * They will still record fork/mmap/etc. so
     * we can keep an accurate log of process creation
     */
    if (collector->counters)
    {
	d_print ("disable counters\n");
	
	disable_counters (collector);
    }
    
    if (collector->tracker)
    {
	tracker_free (collector->tracker);
	collector->tracker = tracker_new ();
    }

    collector->n_samples = 0;
    collector->prev_samples = 0;
    
    g_get_current_time (&collector->latest_reset);

    if (collector->counters)
    {
	d_print ("enable counters\n");
	
	enable_counters (collector);
    }
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
    d_print ("pid, tid: %d %d", comm->pid, comm->tid);
    
    tracker_add_process (collector->tracker,
			 comm->pid,
			 comm->comm);
}

static void
process_fork (Collector *collector, fork_event_t *fork)
{
    d_print ("ppid: %d  pid: %d   ptid: %d  tid %d",
	     fork->ppid, fork->pid, fork->ptid, fork->tid);
    
    tracker_add_fork (collector->tracker, fork->ppid, fork->pid);
}

static void
process_exit (Collector *collector, exit_event_t *exit)
{
    d_print ("for %d %d", exit->pid, exit->tid);
    
    tracker_add_exit (collector->tracker, exit->pid);
}

static void
process_sample (Collector      *collector,
		sample_event_t *sample)
{
    uint64_t *ips;
    int n_ips;

    d_print ("pid, tid: %d %d", sample->pid, sample->tid);
    
    if (sample->n_ips == 0)
    {
	uint64_t trace[3];

	if (sample->header.misc & PERF_EVENT_MISC_KERNEL)
	{
	    trace[0] = PERF_CONTEXT_KERNEL;
	    trace[1] = sample->ip;
	    trace[2] = PERF_CONTEXT_USER;

	    ips = trace;
	    n_ips = 3;
	}
	else
	{
	    trace[0] = PERF_CONTEXT_USER;
	    trace[1] = sample->ip;

	    ips = trace;
	    n_ips = 2;
	}
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
	       counter_t       *counter,
	       counter_event_t *event)
{
    char *name;
    
    switch (event->header.type)
    {
    case PERF_EVENT_MMAP: name = "mmap"; break;
    case PERF_EVENT_LOST: name = "lost"; break;
    case PERF_EVENT_COMM: name = "comm"; break;
    case PERF_EVENT_EXIT: name = "exit"; break;
    case PERF_EVENT_THROTTLE: name = "throttle"; break;
    case PERF_EVENT_UNTHROTTLE: name = "unthrottle"; break;
    case PERF_EVENT_FORK: name = "fork"; break;
    case PERF_EVENT_READ: name = "read"; break;
    case PERF_EVENT_SAMPLE: name = "samp"; break;
    default: name = "unknown"; break;
    }

    d_print ("cpu %d  ::  %s   :: ", counter->cpu, name);
    
    switch (event->header.type)
    {
    case PERF_EVENT_MMAP:
	process_mmap (collector, &event->mmap);
	break;
	
    case PERF_EVENT_LOST:
	g_print ("lost event\n");
	break;
	
    case PERF_EVENT_COMM:
	process_comm (collector, &event->comm);
	break;
	
    case PERF_EVENT_EXIT:
	process_exit (collector, &event->exit);
	break;
	
    case PERF_EVENT_THROTTLE:
	g_print ("throttle\n");
	break;
	
    case PERF_EVENT_UNTHROTTLE:
	g_print ("unthrottle\n");
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
	g_warning ("unknown event: %d (%d)\n",
		   event->header.type, event->header.size);
	break;
    }

    d_print ("\n");
}

gboolean
collector_start (Collector  *collector,
		 GError    **err)
{
    int n_cpus = get_n_cpus ();
    int i;

    if (!collector->tracker)
	collector->tracker = tracker_new ();
    
    for (i = 0; i < n_cpus; ++i)
    {
	counter_t *counter = counter_new (collector, i, err);

	if (!counter)
	{
	    GList *list;
	    
	    for (list = collector->counters; list != NULL; list = list->next)
		counter_free (list->data);

	    collector->tracker = NULL;
	    
	    return FALSE;
	}
	
	collector->counters = g_list_append (collector->counters, counter);
    }

    enable_counters (collector);
    
    return TRUE;
}

void
collector_stop (Collector *collector)
{
    GList *list;

    if (!collector->counters)
	return;
    
    /* Read any remaining data */
    for (list = collector->counters; list != NULL; list = list->next)
    {
	counter_t *counter = list->data;
	
	on_read (counter);
    
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
    /* The collector must be stopped when you create a profile */
    g_assert (!collector->counters);
    
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
