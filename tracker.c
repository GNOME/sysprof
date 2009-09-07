#include <glib.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "tracker.h"
#include "stackstash.h"
#include "binfile.h"

typedef struct new_process_t new_process_t;
typedef struct new_map_t new_map_t;
typedef struct sample_t sample_t;

struct tracker_t
{
    StackStash *stash;

    size_t	n_event_bytes;
    size_t	n_allocated_bytes;
    uint8_t *	events;
};

typedef enum
{
    NEW_PROCESS,
    NEW_MAP,
    SAMPLE
} event_type_t;

struct new_process_t
{
    event_type_t	type;
    int32_t		pid;
    char		command_line[256];
};

struct new_map_t
{
    event_type_t	type;
    int32_t		pid;
    char		filename[PATH_MAX];
    uint64_t		start;
    uint64_t		end;
    uint64_t		offset;
    uint64_t		inode;
};

struct sample_t
{
    event_type_t	type;
    int32_t		pid;
    StackNode *		trace;
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

static char **
get_lines (const char *format, pid_t pid)
{
    char *filename = g_strdup_printf (format, pid);
    char **result = NULL;
    char *contents;

    if (g_file_get_contents (filename, &contents, NULL, NULL))
    {
        result = g_strsplit (contents, "\n", -1);

        g_free (contents);
    }

    g_free (filename);

    return result;
}

static void
fake_new_process (tracker_t *tracker, pid_t pid)
{
    char **lines;

    if ((lines = get_lines ("/proc/%d/status", pid)))
    {
        int i;

        for (i = 0; lines[i] != NULL; ++i)
        {
            if (strncmp ("Name:", lines[i], 5) == 0)
            {
		tracker_add_process (tracker, pid, g_strstrip (strchr (lines[i], ':')));
                break;
            }
        }

        g_strfreev (lines);
    }
}

static void
fake_new_map (tracker_t *tracker, pid_t pid)
{
    char **lines;

    if ((lines = get_lines ("/proc/%d/maps", pid)))
    {
        int i;

        for (i = 0; lines[i] != NULL; ++i)
        {
            char file[256];
            gulong start;
            gulong end;
            gulong offset;
            gulong inode;
            int count;

	    file[255] = '\0';
	    
            count = sscanf (
                lines[i], "%lx-%lx %*15s %lx %*x:%*x %lu %255s",
                &start, &end, &offset, &inode, file);

            if (count == 5)
            {
                if (strcmp (file, "[vdso]") == 0)
                {
                    /* For the vdso, the kernel reports 'offset' as the
                     * the same as the mapping addres. This doesn't make
                     * any sense to me, so we just zero it here. There
                     * is code in binfile.c (read_inode) that returns 0
                     * for [vdso].
                     */
                    offset = 0;
                    inode = 0;
                }

		tracker_add_map (tracker, pid, start, end, offset, inode, file);
            }
        }

        g_strfreev (lines);
    }
}

static void
populate_from_proc (tracker_t *tracker)
{
    GDir *proc = g_dir_open ("/proc", 0, NULL);
    const char *name;

    if (!proc)
        return;

    while ((name = g_dir_read_name (proc)))
    {
        pid_t pid;
        char *end;

        pid = strtol (name, &end, 10);

        if (*end == 0)
        {
	    fake_new_process (tracker, pid);
	    fake_new_map (tracker, pid);
	}
    }

    g_dir_close (proc);
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

tracker_t *
tracker_new (void)
{
    tracker_t *tracker = g_new0 (tracker_t, 1);

    tracker->n_event_bytes = 0;
    tracker->n_allocated_bytes = DEFAULT_SIZE;
    tracker->events = g_malloc (DEFAULT_SIZE);

    tracker->stash = stack_stash_new (NULL);

    GTimeVal before, after;

    g_get_current_time (&before);
    
    populate_from_proc (tracker);

    g_get_current_time (&after);

    g_print ("Time to populate %f\n", time_diff (&after, &before));
    
    return tracker;
}

void
tracker_free (tracker_t *tracker)
{
    stack_stash_unref (tracker->stash);
    g_free (tracker->events);
    g_free (tracker);
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

#if 0
    g_print ("Added new process: %d (%s)\n", pid, command_line);
#endif
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
    COPY_STRING (event.filename, filename);
    event.start = start;
    event.end = end;
    event.offset = offset;
    event.inode = inode;

    tracker_append (tracker, &event, sizeof (event));

#if 0
    g_print ("   Added new map: %d (%s)\n", pid, filename);
#endif
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

/*  */
typedef struct state_t state_t;
typedef struct process_t process_t;
typedef struct map_t map_t;

struct process_t
{
    pid_t       pid;

    char *      comm;
    char *      undefined;

    GPtrArray *	maps;
};

struct map_t
{
    char *	filename;
    uint64_t	start;
    uint64_t	end;
    uint64_t	offset;
    uint64_t	inode;

    BinFile *	bin_file;
};

struct state_t
{
    GHashTable *processes_by_pid;
};

static void
destroy_map (map_t *map)
{
    if (map->bin_file)
	bin_file_free (map->bin_file);
    
    g_free (map->filename);
    g_free (map);
}

static void
create_map (state_t *state, new_map_t *new_map)
{
    process_t *process;
    map_t *map;
    int i;
    
    process = g_hash_table_lookup (
	state->processes_by_pid, GINT_TO_POINTER (new_map->pid));

    if (!process)
	return;

    map = g_new0 (map_t, 1);
    map->filename = g_strdup (new_map->filename);
    map->start = new_map->start;
    map->end = new_map->end;
    map->offset = new_map->offset;
    map->inode = new_map->inode;

    /* Remove existing maps that overlap the new one */
    for (i = 0; i < process->maps->len; ++i)
    {
        map_t *m = process->maps->pdata[i];

        if (m->start < map->end && m->end > map->start)
        {
            destroy_map (m);
	    
            g_ptr_array_remove_index (process->maps, i);
        }
    }

    g_ptr_array_add (process->maps, map);
}

static void
destroy_process (process_t *process)
{
    int i;
    
    g_free (process->comm);
    g_free (process->undefined);
    
    for (i = 0; i < process->maps->len; ++i)
    {
	map_t *map = process->maps->pdata[i];
	
	destroy_map (map);
    }
    
    g_ptr_array_free (process->maps, TRUE);
    g_free (process);
}

static void
create_process (state_t *state, new_process_t *new_process)
{
    process_t *process = g_new0 (process_t, 1);

    process->pid = new_process->pid;
    process->comm = g_strdup (new_process->command_line);
    process->undefined = NULL;
    process->maps = g_ptr_array_new ();

    g_hash_table_insert (
	state->processes_by_pid, GINT_TO_POINTER (process->pid), process);
}

static void
free_process (gpointer data)
{
    process_t *process = data;
    
    destroy_process (process);
}

static state_t *
state_new (void)
{
    state_t *state = g_new0 (state_t, 1);

    state->processes_by_pid =
	g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, free_process);

    return state;
}    

static void
process_sample (state_t *tracker, sample_t *sample)
{
}

Profile *
tracker_create_profile (tracker_t *tracker)
{
    uint8_t *end = tracker->events + tracker->n_event_bytes;
    StackStash *resolved_stash;
    Profile *profile;
    state_t *state;
    uint8_t *event;
    
    state = state_new ();
    resolved_stash = stack_stash_new (g_free);
    
    event = tracker->events;
    while (event < end)
    {
	event_type_t type = *(event_type_t *)event;

	switch (type)
	{
	case NEW_PROCESS:
	    create_process (state, (new_process_t *)event);
	    break;
	    
	case NEW_MAP:
	    create_map (state, (new_map_t *)event);
	    break;
	    
	case SAMPLE:
	    process_sample (state, (sample_t *)event);
	    break;
	}
	
    }

    profile = profile_new (resolved_stash);

    stack_stash_unref (resolved_stash);

    return profile;
}
