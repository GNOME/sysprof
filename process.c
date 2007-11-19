/* MemProf -- memory profiler and leak detector
 * Copyright 1999, 2000, 2001, Red Hat, Inc.
 * Copyright 2002, Kristian Rietveld
 *
 * Sysprof -- Sampling, systemwide CPU profiler
 * Copyright 2004-2007 Soeren Sandmann
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

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

#include "process.h"
#include "binfile.h"

static GHashTable *processes_by_pid;

typedef struct Map Map;
struct Map
{
    char *	filename;
    gulong	start;
    gulong	end;
    gulong      offset;
    gulong	inode;
    
    BinFile *	bin_file;
};

struct Process
{
    char *	cmdline;
    
    int		n_maps;
    Map *	maps;
    
    GList *	bad_pages;
    
    int		pid;
    
    char *	undefined;
};

static void
initialize (void)
{
    if (!processes_by_pid)
	processes_by_pid = g_hash_table_new (g_direct_hash, g_direct_equal);
}

static Map *
read_maps (int pid, int *n_maps)
{
    char *name = g_strdup_printf ("/proc/%d/maps", pid);
    char buffer[1024];
    FILE *in;
    GArray *result;

    in = fopen (name, "r");
    if (!in)
    {
	g_free (name);
	return NULL;
    }

    result = g_array_new (FALSE, FALSE, sizeof (Map));
    
    while (fgets (buffer, sizeof (buffer) - 1, in))
    {
	char file[256];
	int count;
	gulong start;
	gulong end;
	gulong offset;
	gulong inode;
	
	count = sscanf (
	    buffer, "%lx-%lx %*15s %lx %*x:%*x %lu %255s", 
	    &start, &end, &offset, &inode, file);
	if (count == 5)
	{
	    Map map;
	    
	    map.filename = g_strdup (file);
	    map.start = start;
	    map.end = end;
	    
	    if (strcmp (map.filename, "[vdso]") == 0)
	    {
		/* For the vdso, the kernel reports 'offset' as the
		 * the same as the mapping addres. This doesn't make
		 * any sense to me, so we just zero it here. There
		 * is code in binfile.c (read_inode) that returns 0
		 * for [vdso].
		 */
		map.offset = 0;
		map.inode = 0;
	    }
	    else
	    {
		map.offset = offset;
		map.inode = inode;
	    }

	    map.bin_file = NULL;

	    g_array_append_val (result, map);
	}
    }

    g_free (name);
    fclose (in);

    if (n_maps)
	*n_maps = result->len;

    return (Map *)g_array_free (result, FALSE);
}

static void
free_maps (int *n_maps,
	   Map *maps)
{
    int i;

    for (i = 0; i < *n_maps; ++i)
    {
	Map *map = &(maps[i]);
	
	if (map->filename)
	    g_free (map->filename);
	
	if (map->bin_file)
	    bin_file_free (map->bin_file);
    }

    g_free (maps);
    *n_maps = 0;
}

const guint8 *
process_get_vdso_bytes (gsize *length)
{
    static gboolean has_data;
    static const guint8 *bytes = NULL;
    static gsize n_bytes = 0;    

    if (!has_data)
    {
	Map *maps;
	int n_maps, i;

	maps = read_maps (getpid(), &n_maps);

	for (i = 0; i < n_maps; ++i)
	{
	    Map *map = &(maps[i]);

	    if (strcmp (map->filename, "[vdso]") == 0)
	    {
		n_bytes = map->end - map->start;

		/* Dup the memory here so that valgrind will only
		 * report one 1 byte invalid read instead of
		 * a ton when the elf parser scans the vdso
		 *
		 * The reason we get a spurious invalid read from
		 * valgrind is that we are getting the address directly
		 * from /proc/maps, and valgrind knows that its mmap()
		 * wrapper never returned that address. But since it
		 * is a legal mapping, it is legal to read it.
		 */
		bytes = g_memdup ((guint8 *)map->start, n_bytes);
	    }
	}
	
	has_data = TRUE;
	free_maps (&n_maps, maps);
    }

    if (length)
	*length = n_bytes;

    return bytes;
}

static Process *
create_process (const char *cmdline, int pid)
{
    Process *p;
    
    p = g_new0 (Process, 1);
    
    if (*cmdline != '\0')
	p->cmdline = g_strdup_printf ("[%s]", cmdline);
    else
	p->cmdline = g_strdup_printf ("[pid %d]", pid);
    
    p->bad_pages = NULL;
    p->n_maps = 0;
    p->maps = NULL;
    p->pid = pid;
    p->undefined = NULL;
    
    g_assert (!g_hash_table_lookup (processes_by_pid, GINT_TO_POINTER (pid)));
    
    g_hash_table_insert (processes_by_pid, GINT_TO_POINTER (pid), p);
    
    return p;
}

static Map *
process_locate_map (Process *process, gulong addr)
{
    int i;

    for (i = 0; i < process->n_maps; ++i)
    {
	Map *map = &(process->maps[i]);

	if ((addr >= map->start) &&
	    (addr < map->end))
	{
	    if (i > 4)
	    {
		/* FIXME: Is this move-to-front really worth it? */
		Map tmp = *map;

		memmove (process->maps + 1, process->maps, i * sizeof (Map));

		*(process->maps) = tmp;

		map = process->maps;
	    }
	    
	    return map;
	}
    }
    
    return NULL;
}

static void
free_process (gpointer key, gpointer value, gpointer data)
{
    Process *process = value;
    
    free_maps (&(process->n_maps), process->maps);

    g_free (process->undefined);
    g_free (process->cmdline);
    g_list_free (process->bad_pages);
    
    g_free (process);
}

void
process_flush_caches  (void)
{
    if (!processes_by_pid)
	return;
    
    g_hash_table_foreach (processes_by_pid, free_process, NULL);
    g_hash_table_destroy (processes_by_pid);
    
    processes_by_pid = NULL;
}


static gboolean
process_has_page (Process *process, gulong addr)
{
    if (process_locate_map (process, addr))
	return TRUE;
    else
	return FALSE;
}

static int
page_size (void)
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

void
process_ensure_map (Process *process, int pid, gulong addr)
{
    /* Round down to closest page */
    
    addr = (addr - addr % page_size());
    
    if (process_has_page (process, addr))
	return;
    
    if (g_list_find (process->bad_pages, (gpointer)addr))
	return;
    
    /* a map containing addr was not found */
    if (process->maps)
	free_maps (&(process->n_maps), process->maps);
    
    process->maps = read_maps (pid, &(process->n_maps));

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
    
    if (g_file_get_contents (filename, &stat, NULL, NULL))
    {
	char result[200];
	
	idle_free (stat);
	
	if (sscanf (stat, "%*d %200s %*s", result) == 1)
	    return g_strndup (result, 200);
    }
    
    return NULL;
}

static char *
get_pidname (int pid)
{
    if (pid == -1)
	return g_strdup_printf ("kernel");
    else
	return g_strdup_printf ("pid %d", pid);
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
    
    initialize();
    
    p = g_hash_table_lookup (processes_by_pid, GINT_TO_POINTER (pid));
    
    if (!p)
	p = create_process (idle_free (get_name (pid)), pid);
    
    return p;
}

static gboolean
file_exists (const char *name)
{
    int fd;
    fd = open (name, O_RDONLY);
    
    if (fd > 0)
    {
	close (fd);
	return TRUE;
    }
    return FALSE;
}

static gchar *
look_for_vmlinux (void)
{
    struct utsname utsname;
    char *result;
    char **s;
    char *names[4];

    uname (&utsname);

    names[0] = g_strdup_printf (
	"/usr/lib/debug/lib/modules/%s/vmlinux", utsname.release);
    names[1] = g_strdup_printf (
	"/lib/modules/%s/source/vmlinux", utsname.release);
    names[2] = g_strdup_printf (
	"/boot/vmlinux-%s", utsname.release);
    names[3] = NULL;

    result = NULL;
    for (s = names; *s; s++)
    {
	if (file_exists (*s))
	{
	    result = g_strdup (*s);
	    break;
	}
    }

    for (s = names; *s; s++)
	g_free (*s);

    return result;
}

static const gchar *
find_kernel_binary (void)
{
    static gboolean looked_for_vmlinux;
    static gchar *binary = NULL;
    
    if (!looked_for_vmlinux)
    {
	binary = look_for_vmlinux ();
	looked_for_vmlinux = TRUE;
    }
    
    return binary;
}

typedef struct
{
    gulong	 address;
    char	*name;
} KernelSymbol;

static void
parse_kallsym_line (const char *line,
		    GArray *table)
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
	    KernelSymbol sym;

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
    const KernelSymbol *sym_a = a;
    const KernelSymbol *sym_b = b;

    if (sym_a->address > sym_b->address)
	return 1;
    else if (sym_a->address == sym_b->address)
	return 0;
    else
	return -1;
}

static GArray *
get_kernel_symbols (void)
{
    static GArray *kernel_syms;
    static gboolean initialized = FALSE;

    find_kernel_binary();
    
    if (!initialized)
    {
	char *kallsyms;
	if (g_file_get_contents ("/proc/kallsyms", &kallsyms, NULL, NULL))
	{
	    if (kallsyms)
	    {
		kernel_syms = g_array_new (TRUE, TRUE, sizeof (KernelSymbol));
		
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
	}

	if (!kernel_syms)
	    g_print ("Warning: /proc/kallsyms could not be "
		     "read. Kernel symbols will not be available\n");
	
	initialized = TRUE;
    }

    return kernel_syms;
}

gboolean
process_is_kernel_address (gulong address)
{
    GArray *ksyms = get_kernel_symbols ();

    if (ksyms &&
	address >= g_array_index (ksyms, KernelSymbol, 0).address	&&
	address <  g_array_index (ksyms, KernelSymbol, ksyms->len - 1).address)
    {
	return TRUE;
    }

    return FALSE;
}

static KernelSymbol *
do_lookup (KernelSymbol *symbols,
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

const char *
process_lookup_kernel_symbol (gulong address,
			      gulong *offset)
{
    GArray *ksyms = get_kernel_symbols ();
    KernelSymbol *result;

    if (ksyms->len == 0)
	return NULL;
    
    result = do_lookup ((KernelSymbol *)ksyms->data, address, 0, ksyms->len - 1);
    if (result && offset)
	*offset = address - result->address;
    
    return result? result->name : NULL;
}

const char *
process_lookup_symbol (Process *process, gulong address)
{
    static const char *const kernel = "kernel";
    const BinSymbol *result;
    Map *map = process_locate_map (process, address);
    
/*     g_print ("addr: %x\n", address); */

    if (address == 0x1)
    {
	return kernel;
    }
    else if (!map)
    {
	gulong offset;
	const char *res = process_lookup_kernel_symbol (address, &offset);
	
	if (res && offset != 0)
	    return res;
	
	if (!process->undefined)
	{
	    process->undefined =
		g_strdup_printf ("No map (%s)", process->cmdline);
	}

	return process->undefined;
    }

#if 0
    if (strcmp (map->filename, "/home/ssp/sysprof/sysprof") == 0)
    {
	g_print ("YES\n");

	g_print ("map address: %lx\n", map->start);
	g_print ("map offset: %lx\n", map->offset);
	g_print ("address before: %lx  (%s)\n", address, map->filename);
    }

    g_print ("address before: \n");
#endif

#if 0
    g_print ("%s is mapped at %lx + %lx\n", map->filename, map->start, map->offset);
    g_print ("incoming address: %lx\n", address);
#endif
    
    address -= map->start;
    address += map->offset;

#if 0
    if (strcmp (map->filename, "[vdso]") == 0)
    {
	g_print ("address after: %lx\n", address);
    }
#endif
    
    if (!map->bin_file)
	map->bin_file = bin_file_new (map->filename);

/*     g_print ("%s: start: %p, load: %p\n", */
/* 	     map->filename, map->start, bin_file_get_load_address (map->bin_file)); */

    if (!bin_file_check_inode (map->bin_file, map->inode))
    {
	/* If the inodes don't match, it's probably because the
	 * file has changed since the process was started. Just return
	 * the undefined symbol in that case.
	 */
	address = 0x0;
    }

    result = bin_file_lookup_symbol (map->bin_file, address);
    
#if 0
    g_print (" ---> %s\n", result->name);
#endif
    
/*     g_print ("(%x) %x %x name; %s\n", address, map->start, map->offset, result->name); */
    
    return bin_symbol_get_name (map->bin_file, result);
}

const char *
process_get_cmdline (Process *process)
{
    return process->cmdline;
}
