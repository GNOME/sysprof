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

void
process_ensure_map (Process *process, int pid, gulong addr)
{
    /* Round down to closest page */
    
    addr = (addr - addr % process_get_page_size());
    
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
    static const char names[][48] = {
	"/lib/modules/%s/build/vmlinux",
	"/usr/lib/debug/lib/modules/%s/vmlinux",
	"/lib/modules/%s/source/vmlinux",
	"/boot/vmlinux-%s",
    };
    struct utsname utsname;
    int i;
    
    uname (&utsname);

    for (i = 0; i < G_N_ELEMENTS (names); ++i)
    {
	char *filename = g_strdup_printf (names[i], utsname.release);

	if (file_exists (filename))
	    return filename;

	g_free (filename);
    }

    return NULL;
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

const char *
process_get_cmdline (Process *process)
{
    return process->cmdline;
}
