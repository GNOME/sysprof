#include <glib.h>
#include <string.h>

#include "tracker.h"
#include "stackstash.h"

typedef struct new_process_t new_process_t;
typedef struct new_map_t new_map_t;
typedef struct sample_t sample_t;

struct tracker_t
{
    StackStash *stash;
    
    size_t	 n_event_bytes;
    size_t	 n_allocated_bytes;
    uint8_t	*events;
};

typedef enum
{
    NEW_PROCESS,
    NEW_MAP,
    SAMPLE
} event_type;

struct new_process_t
{
    event_type	type;
    int32_t	pid;
    char	command_line[256];
};

struct new_map_t
{
    event_type	type;
    int32_t	pid;
    char	file_name[PATH_MAX];
    uint64_t	start;
    uint64_t	end;
    uint64_t	offset;
    uint64_t	inode;
};

struct sample_t
{
    event_type	type;
    int32_t	pid;
    StackNode *	trace;
};

#define DEFAULT_SIZE	(1024 * 1024 * 4)

static void
tracker_append (tracker_t *tracker,
		void      *event,
		int        n_bytes)
{
    if (tracker->n_allocated_bytes - tracker->n_event_bytes < n_bytes)
    {
	size_t new_size = tracker->n_allocated_bytes * 2;
	
	tracker->events = g_realloc (tracker->events, new_size);

	tracker->n_allocated_bytes = new_size;
    }

    g_assert (tracker->n_allocated_bytes - tracker->n_event_bytes >= n_bytes);

    memcpy (tracker->events + tracker->n_event_bytes, event, n_bytes);
}

tracker_t *
tracker_new (void)
{
    tracker_t *tracker = g_new0 (tracker_t, 1);

    tracker->n_event_bytes = 0;
    tracker->n_allocated_bytes = DEFAULT_SIZE;
    tracker->events = g_malloc (DEFAULT_SIZE);

    return tracker;
}

void
tracker_free (tracker_t *tracker)
{
    
}

#define COPY_STRING(dest, src)						\
    do									\
    {									\
	strncpy (dest, src, sizeof (dest) - 1);				\
	dest[sizeof (dest) - 1] = 0;					\
    }									\
    while (0)
    

void
tracker_add_process (tracker_t * tracker,
		     pid_t	 pid,
		     const char *command_line)
{
    new_process_t event;

    event.type = NEW_PROCESS;
    event.pid = pid;
    COPY_STRING (event.command_line, command_line);

    tracker_append (tracker, &event, sizeof (event));
}

void
tracker_add_map (tracker_t * tracker,
		 pid_t	     pid,
		 uint64_t    start,
		 uint64_t    end,
		 uint64_t    offset,
		 uint64_t    inode,
		 const char *filename)
{
    new_map_t event;

    event.type = NEW_MAP;
    event.pid = pid;
    COPY_STRING (event.file_name, filename);
    event.start = start;
    event.end = end;
    event.offset = offset;
    event.inode = inode;

    tracker_append (tracker, &event, sizeof (event));
}

void
tracker_add_sample  (tracker_t *tracker,
		     pid_t	pid,
		     uint64_t  *ips,
		     int        n_ips)
{
    sample_t event;

    event.type = SAMPLE;
    event.pid = pid;
    event.trace = stack_stash_add_trace (tracker->stash, ips, n_ips, 1);

    tracker_append (tracker, &event, sizeof (event));
}

Profile *
tracker_create_profile (tracker_t *tracker)
{
    
}
