#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>

#include "process.h"
#include "binfile.h"

/* FIXME: All the interesting stuff in this file is from memprof
 * and copyright Red Hat.
 */

/* FIXME: this should  be done with getpagesize() */
#define PAGE_SIZE 4096

static GHashTable *processes_by_cmdline;
static GHashTable *processes_by_pid;

typedef struct Map Map;
struct Map
{
    char *	filename;
    gulong	start;
    gulong	end;
    gulong      offset;
#if 0
    gboolean	do_offset;
#endif
    
    BinFile *	bin_file;
};

struct Process
{
    char *cmdline;
    
    GList *maps;
    GList *bad_pages;

    int pid;
};

static void
initialize (void)
{
    if (!processes_by_cmdline)
    {
	processes_by_cmdline = g_hash_table_new (g_str_hash, g_str_equal);
	processes_by_pid = g_hash_table_new (g_direct_hash, g_direct_equal);
    }
}

static GList *
read_maps (int pid)
{
    char *name = g_strdup_printf ("/proc/%d/maps", pid);
    char buffer[1024];
    FILE *in;
    GList *result = NULL;
    
    in = fopen (name, "r");
    if (!in)
    {
#if 0
	g_print ("could not open %d: %s\n", pid, g_strerror (errno));
#endif
	g_free (name);
	return NULL;
    }
    
    while (fgets (buffer, sizeof (buffer) - 1, in))
    {
	char file[256];
	int count;
	guint start;
	guint end;
	guint offset;

#if 0
	g_print ("buffer: %s\n", buffer);
#endif
	
	count = sscanf (
	    buffer, "%x-%x %*15s %x %*x:%*x %*u %255s", 
	    &start, &end, &offset, file);
	if (count == 4)
	{
	    Map *map;

	    map = g_new (Map, 1);
	    
	    map->filename = g_strdup (file);
	    map->start = start;
	    map->end = end;
	    
	    map->offset = offset;
	    
	    map->bin_file = NULL;
	    
	    result = g_list_prepend (result, map);
	}
#if 0
	else
	{
	    g_print ("scanf\n");
	}
#endif
    }
    
    g_free (name);
    fclose (in);
    return result;
}


static Process *
create_process (const char *cmdline, int pid)
{
    Process *p;
    
    p = g_new (Process, 1);

    if (*cmdline != '\0')
	p->cmdline = g_strdup_printf ("[%s]", cmdline);
    else
	p->cmdline = g_strdup_printf ("[pid %d]", pid);

    p->bad_pages = NULL;
    p->maps = NULL;
    p->pid = pid;
    
    g_assert (!g_hash_table_lookup (processes_by_pid, GINT_TO_POINTER (pid)));
    g_assert (!g_hash_table_lookup (processes_by_cmdline, cmdline));
    
    g_hash_table_insert (processes_by_pid, GINT_TO_POINTER (pid), p);
    g_hash_table_insert (processes_by_cmdline, g_strdup (cmdline), p);
    
    return p;
}

static Map *
process_locate_map (Process *process, gulong addr)
{
    GList *list;
    
    for (list = process->maps; list != NULL; list = list->next)
    {
	Map *map = list->data;
	
	if ((addr >= map->start) &&
	    (addr < map->end))
	{
	    return map;
	}
    }
    
    return NULL;
}

static void
process_free_maps (Process *process)
{
    GList *list;
    
    for (list = process->maps; list != NULL; list = list->next)
    {
	Map *map = list->data;
	
	if (map->bin_file)
	    bin_file_free (map->bin_file);
	
	g_free (map);
    }
    
    g_list_free (process->maps);
}

static gboolean
process_has_page (Process *process, gulong addr)
{
    if (process_locate_map (process, addr))
	return TRUE;
    else
	return FALSE;
}

void
process_ensure_map (Process *process, int pid, gulong addr)
{
    /* Round down to closest page */

    addr = (addr - addr % PAGE_SIZE);

    if (process_has_page (process, addr))
	return;
    
    if (g_list_find (process->bad_pages, (gpointer)addr))
	return;

    /* a map containing addr was not found */
    if (process->maps)
	process_free_maps (process);
    
    process->maps = read_maps (pid);
    
    if (!process_has_page (process, addr))
    {
#if 0
	g_print ("Bad page: %p\n", addr);
#endif
	process->bad_pages = g_list_prepend (process->bad_pages, (gpointer)addr);
    }
}

static gboolean
do_idle_free (gpointer d)
{
    g_free (d);
    return FALSE;
}

static char *
idle_free (char *d)
{
    g_idle_add (do_idle_free, d);
    return d;
}

static char *
get_cmdline (int pid)
{
    char *cmdline;
    char *filename = idle_free (g_strdup_printf ("/proc/%d/cmdline", pid));

    if (g_file_get_contents (filename, &cmdline, NULL, NULL))
    {
	if (*cmdline == '\0')
	{
	    g_free (cmdline);
	    return NULL;
 	}
	return cmdline;
    }

    return NULL;
}

static char *
get_statname (int pid)
{
    char *stat;
    char *filename = idle_free (g_strdup_printf ("/proc/%d/stat", pid));

#if 0
    g_print ("stat %d\n", pid);
#endif
    
    if (g_file_get_contents (filename, &stat, NULL, NULL))
    {
	char result[200];
	
	idle_free (stat);

	if (sscanf (stat, "%*d %200s %*s", result) == 1)
	    return g_strndup (result, 200);
    }
#if 0
    g_print ("return null\n");
#endif
    
    return NULL;
}

static char *
get_pidname (int pid)
{
    return g_strdup_printf ("[pid %d]", pid);
}

static char *
get_name (int pid)
{
    char *cmdline = NULL;

    if ((cmdline = get_cmdline (pid)))
	return cmdline;

    if ((cmdline = get_statname (pid)))
	return cmdline;

    return get_pidname (pid);
}

Process *
process_get_from_pid (int pid)
{
    Process *p;
    gchar *cmdline = NULL;
    
    initialize();

    p = g_hash_table_lookup (processes_by_pid, GINT_TO_POINTER (pid));

    if (p)
	return p;
    
    cmdline = get_name (pid);

    p = g_hash_table_lookup (processes_by_cmdline, cmdline);
    if (p)
	return p;
    else
	return create_process (idle_free (cmdline), pid);
}

const Symbol *
process_lookup_symbol (Process *process, gulong address)
{
    static Symbol undefined;
    const Symbol *result;
    Map *map = process_locate_map (process, address);

/*     g_print ("addr: %x\n", address); */
    
    if (!map)
    {
	if (undefined.name)
	    g_free (undefined.name);
	undefined.name = g_strdup_printf ("??? %s", process->cmdline);
	undefined.address = 0xBABE0001;

#if 0
	g_print ("no map for %p (%s)\n", address, process->cmdline);
#endif
	
	return &undefined;
    }

#if 0
    g_print ("has map: %s\n", process->cmdline);
#endif
    
/*     if (map->do_offset) */
/*       address -= map->start; */

    /* convert address to file coordinates */
    address -= map->start;
    address += map->offset;

    if (!map->bin_file)
	map->bin_file = bin_file_new (map->filename);
    
/*     g_print ("%s: start: %p, load: %p\n", */
/* 	     map->filename, map->start, bin_file_get_load_address (map->bin_file)); */
    
     result = bin_file_lookup_symbol (map->bin_file, address);

/*     g_print ("(%x) %x %x name; %s\n", address, map->start, map->offset, result->name); */

    return result;
}

#if 0
const Symbol *
process_lookup_symbol_with_filename (Process *process,
				     int pid,
				     gulong map_start,
				     const char *filename,
				     gulong address)
{
    const Symbol *result;
    BinFile *bin_file;

    if (!filename)
	return process_lookup_symbol (process, address);

    bin_file = bin_file_new (filename);

    if (should_offset (filename, pid))
	address -= map_start;
    
    result = bin_file_lookup_symbol (bin_file, address);

    bin_file_free (bin_file);

    return result;
}
#endif

const char *
process_get_cmdline (Process *process)
{
    return process->cmdline;
}

static void
free_process (gpointer key, gpointer value, gpointer data)
{
    char *cmdline = key;
    Process *process = value;
    
#if 0
    g_print ("freeing: %p\n", process);
    memset (process, '\0', sizeof (Process));
#endif
    g_free (process->cmdline);
#if 0
    process->cmdline = "You are using free()'d memory";
#endif
    process_free_maps (process);
    g_list_free (process->bad_pages);
    g_free (cmdline);
    
    g_free (process);
}

void
process_flush_caches  (void)
{
    if (!processes_by_cmdline)
	return;
    g_hash_table_foreach (processes_by_cmdline, free_process, NULL);
    
    g_hash_table_destroy (processes_by_cmdline);
    g_hash_table_destroy (processes_by_pid);
    
    processes_by_cmdline = NULL;
    processes_by_pid = NULL;
}
