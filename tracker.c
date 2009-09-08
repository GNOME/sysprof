#include <glib.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib/gprintf.h>

#include "tracker.h"
#include "stackstash.h"
#include "binfile.h"
#include "elfparser.h"
#include "perf_counter.h"

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
		tracker_add_process (
		    tracker, pid, g_strstrip (strchr (lines[i], ':') + 1));
		
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
    GTimeVal before, after;
    
    tracker->n_event_bytes = 0;
    tracker->n_allocated_bytes = DEFAULT_SIZE;
    tracker->events = g_malloc (DEFAULT_SIZE);
    
    tracker->stash = stack_stash_new (NULL);
    
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

    tracker->n_event_bytes += n_bytes;
}

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
    GHashTable *unique_comms;
    GHashTable *unique_symbols;
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
	g_hash_table_new_full (g_direct_hash, g_direct_equal,
			       NULL, free_process);
    
    state->unique_symbols = g_hash_table_new (g_direct_hash, g_direct_equal);
    state->unique_comms = g_hash_table_new (g_str_hash, g_str_equal);
    
    return state;
}    

typedef struct
{
    gulong  	 address;
    char	*name;
} kernel_symbol_t;

static void
parse_kallsym_line (const char *line, GArray *table)
{
    char **tokens = g_strsplit_set (line, " \t", -1);
    
    if (tokens[0] && tokens[1] && tokens[2])
    {
	glong address;
	char *endptr;
	
	address = strtoul (tokens[0], &endptr, 16);
	
	if (*endptr == '\0'			&&
	    (strcmp (tokens[1], "T") == 0	||
	     strcmp (tokens[1], "t") == 0))
	{
	    kernel_symbol_t sym;
	    
	    sym.address = address;
	    sym.name = g_strdup (tokens[2]);
	    
	    g_array_append_val (table, sym);
	}
    }
    
    g_strfreev (tokens);
}

static gboolean
parse_kallsyms (const char *kallsyms,
		GArray     *table)
{
    const char *sol;
    const char *eol;
    
    sol = kallsyms;
    eol = strchr (sol, '\n');
    while (eol)
    {
	char *line = g_strndup (sol, eol - sol);
	
	parse_kallsym_line (line, table);
	
	g_free (line);
	
	sol = eol + 1;
	eol = strchr (sol, '\n');
    }
    
    if (table->len <= 1)
	return FALSE;
    
    return TRUE;
}

static int
compare_syms (gconstpointer a, gconstpointer b)
{
    const kernel_symbol_t *sym_a = a;
    const kernel_symbol_t *sym_b = b;
    
    if (sym_a->address > sym_b->address)
	return 1;
    else if (sym_a->address == sym_b->address)
	return 0;
    else
	return -1;
}

static kernel_symbol_t *
do_lookup (kernel_symbol_t *symbols,
	   gulong        address,
	   int           first,
	   int           last)
{
    if (address >= symbols[last].address)
    {
	return &(symbols[last]);
    }
    else if (last - first < 3)
    {
	while (last >= first)
	{
	    if (address >= symbols[last].address)
		return &(symbols[last]);
	    
	    last--;
	}
	
	return NULL;
    }
    else
    {
	int mid = (first + last) / 2;
	
	if (symbols[mid].address > address)
	    return do_lookup (symbols, address, first, mid);
	else
	    return do_lookup (symbols, address, mid, last);
    }
}

static GArray *
get_kernel_symbols (void)
{
    static GArray *kernel_syms;
    static gboolean initialized = FALSE;
    
#if 0
    find_kernel_binary();
#endif
    
    if (!initialized)
    {
	char *kallsyms;
	if (g_file_get_contents ("/proc/kallsyms", &kallsyms, NULL, NULL))
	{
	    if (kallsyms)
	    {
		kernel_syms = g_array_new (TRUE, TRUE, sizeof (kernel_symbol_t));
		
		if (parse_kallsyms (kallsyms, kernel_syms))
		{
		    g_array_sort (kernel_syms, compare_syms);
		}
		else
		{
		    g_array_free (kernel_syms, TRUE);
		    kernel_syms = NULL;
		}
	    }
	    
	    g_free (kallsyms);
	}
	
	if (!kernel_syms)
	{
	    g_print ("Warning: /proc/kallsyms could not be "
		     "read. Kernel symbols will not be available\n");
	}
	
	initialized = TRUE;
    }
    
    return kernel_syms;
}

const char *
lookup_kernel_symbol (gulong address)
{
    kernel_symbol_t *result;
    GArray *ksyms = get_kernel_symbols ();
    
    if (ksyms->len == 0)
	return NULL;
    
    result = do_lookup ((kernel_symbol_t *)ksyms->data, address, 0, ksyms->len - 1);
    
    return result? result->name : NULL;
}

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

static map_t *
process_locate_map (process_t *process, gulong addr)
{
    int i;
    
    for (i = 0; i < process->maps->len; ++i)
    {
	map_t *map = process->maps->pdata[i];
	
	if (addr >= map->start && addr < map->end)
	    return map;
    }
    
    return NULL;
}

static const char *
make_message (state_t *state, const char *format, ...)
{
    va_list args;
    char *message;
    char *result;

    va_start (args, format);
    g_vasprintf (&message, format, args);
    va_end (args);

    result = g_hash_table_lookup (state->unique_comms, message);
    if (result)
    {
	g_free (message);
    }
    else
    {
	result = message;
	
	g_hash_table_insert (state->unique_comms, result, result);
    }

    return result;
}

static char *
lookup_symbol (state_t    *state,
	       process_t  *process,
	       uint64_t    address,
	       gboolean    kernel)
{
    const char *sym;
    
    g_assert (process);
    
    if (kernel)
    {
	sym = lookup_kernel_symbol (address);
    }
    else
    {
	map_t *map = process_locate_map (process, address);
    
	if (!map)
	{
	    sym = make_message (state, "No map [%s]", process->comm);
	}
	else
	{
	    const BinSymbol *bin_sym;
	    
	    address -= map->start;
	    address += map->offset;
    
	    if (!map->bin_file)
		map->bin_file = bin_file_new (map->filename);
    
	    if (map->inode && !bin_file_check_inode (map->bin_file, map->inode))
	    {
		/* If the inodes don't match, it's probably because the
		 * file has changed since the process was started.
		 */
		sym = make_message (state, "%s: inode mismatch", map->filename);
	    }
	    else
	    {
		bin_sym = bin_file_lookup_symbol (map->bin_file, address);
		
		sym = bin_symbol_get_name (map->bin_file, bin_sym);
	    }
	}
    }
    
    if (sym)
	return unique_dup (state->unique_symbols, sym);
    else
	return NULL;
}

typedef struct context_info_t context_info_t;
struct context_info_t
{
    enum perf_callchain_context	context;
    char			name[32];
};    

static const context_info_t context_info[] =
{
    { PERF_CONTEXT_HV,			"- - hypervisor - -" },
    { PERF_CONTEXT_KERNEL,		"- - kernel - -" },
    { PERF_CONTEXT_USER,		"- - user - - " },
    { PERF_CONTEXT_GUEST,		"- - guest - - " },
    { PERF_CONTEXT_GUEST_KERNEL,	"- - guest kernel - -" },
    { PERF_CONTEXT_GUEST_USER,		"- - guest user - -" },
};

static const char *const everything = "[Everything]";

static const context_info_t *
get_context_info (enum perf_callchain_context context)
{
    int i;
    
    for (i = 0; i < sizeof (context_info) / sizeof (context_info[0]); ++i)
    {
	const context_info_t *info = &context_info[i];
	
	if (info->context == context)
	    return info;
    }
    
    return NULL;
}

static void
process_sample (state_t *state, StackStash *resolved, sample_t *sample)
{
    const context_info_t *context = NULL;
    const char *cmdline;
    uint64_t *resolved_traces;
    process_t *process;
    StackNode *n;
    int len;

    process = g_hash_table_lookup (
	state->processes_by_pid, GINT_TO_POINTER (sample->pid));
    
    if (!process)
    {
	static gboolean warned;
	if (!warned || sample->pid != 0)
	{
	    g_warning ("sample for unknown process %d", sample->pid);
	    warned = TRUE;
	}
	return;
    }
    
    len = 4;
    for (n = sample->trace; n != NULL; n = n->parent)
	len++;
    
    resolved_traces = g_new (uint64_t, len);
    
    len = 0;
    for (n = sample->trace; n != NULL; n = n->parent)
    {
	uint64_t address = n->data;
	const context_info_t *new_context;
	const char *symbol;
	
	new_context = get_context_info (address);
	if (new_context)
	{
	    if (context)
		symbol = unique_dup (state->unique_symbols, context->name);
	    else
		symbol = NULL;
	    
	    context = new_context;
	}
	else
	{
	    gboolean kernel = context && context->context == PERF_CONTEXT_KERNEL;
	    
	    symbol = lookup_symbol (state, process, address, kernel);
	}
	
	if (symbol)
	    resolved_traces[len++] = POINTER_TO_U64 (symbol);
    }

    cmdline = make_message (state, "[%s]", process->comm);
    
    resolved_traces[len++] = POINTER_TO_U64 (cmdline);
    resolved_traces[len++] = POINTER_TO_U64 (
	unique_dup (state->unique_symbols, everything));

    stack_stash_add_trace (resolved, resolved_traces, len, 1);

    g_free (resolved_traces);
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
	    event += sizeof (new_process_t);
	    break;
	    
	case NEW_MAP:
	    create_map (state, (new_map_t *)event);
	    event += sizeof (new_map_t);
	    break;
	    
	case SAMPLE:
	    process_sample (state, resolved_stash, (sample_t *)event);
	    event += sizeof (sample_t);
	    break;
	}
    }

    profile = profile_new (resolved_stash);
    
    stack_stash_unref (resolved_stash);
    
    return profile;
}
