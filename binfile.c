/* MemProf -- memory profiler and leak detector
 * Copyright 1999, 2000, 2001, Red Hat, Inc.
 * Copyright 2002, Kristian Rietveld
 *
 * Sysprof -- Sampling, systemwide CPU profiler
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

/* Most interesting code in this file is lifted from bfdutils.c
 * and process.c from Memprof,
 */
#include "config.h"

#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "binfile.h"
#include "elfparser.h"
#include "process.h"

struct BinFile
{
    int		ref_count;
    
    ElfParser *	elf;

    char *	filename;	
    ino_t	inode;
    
    char *	undefined_name;

    gulong	text_offset;
};

/* FIXME: error handling */
static ino_t
read_inode (const char *filename)
{
    struct stat statbuf;

    if (strcmp (filename, "[vdso]") == 0)
	return (ino_t)-1;

    stat (filename, &statbuf);
    return statbuf.st_ino;
}

static ElfParser *
separate_debug_file_exists (const char *name, guint32 crc)
{
    guint32 file_crc;
    ElfParser *parser = elf_parser_new (name, NULL);

    if (!parser)
	return NULL;

    file_crc = elf_parser_get_crc32 (parser);

    if (file_crc != crc)
    {
	g_print ("warning: %s has wrong crc\n", name);
	
	elf_parser_free (parser);
	
	return NULL;
    }

    return parser;
}

/* FIXME - not10: this should probably be detected by config.h -- find out what gdb does*/
static const char *const debug_file_directory = DEBUGDIR;

static ElfParser *
get_debug_file (ElfParser *elf,
		const char *filename,
		char **new_name)
{
    const char *basename;
    char *dir;
    guint32 crc32;
    char *tries[3];
    int i;
    ElfParser *result;

    if (!elf)
	return NULL;
    
    basename = elf_parser_get_debug_link (elf, &crc32);

    if (!basename)
	return NULL;

    dir = g_path_get_dirname (filename);
    
    tries[0] = g_build_filename (dir, basename, NULL);
    tries[1] = g_build_filename (dir, ".debug", NULL);
    tries[2] = g_build_filename (debug_file_directory, dir, basename, NULL);

    for (i = 0; i < 3; ++i)
    {
	result = separate_debug_file_exists (tries[i], crc32);
	if (result)
	{
	    if (new_name)
		*new_name = g_strdup (tries[i]);
	    break;
	}
    }

    g_free (dir);

    for (i = 0; i < 3; ++i)
	g_free (tries[i]);

    return result;
}

static gboolean
list_contains_name (GList *names, const char *name)
{
    GList *list;
    for (list = names; list != NULL; list = list->next)
    {
	const char *n = list->data;

	if (strcmp (n, name) == 0)
	    return TRUE;
    }

    return FALSE;
}

static ElfParser *
find_separate_debug_file (ElfParser *elf,
			  const char *filename)
{
    ElfParser *debug;
    char *debug_name = NULL;
    char *fname;
    GList *seen_names = NULL;

    fname = g_strdup (filename);

    do
    {
	if (list_contains_name (seen_names, fname))
	{
	    /* cycle detected, just return the original elf file itself */
	    break;
	}
	    
	debug = get_debug_file (elf, fname, &debug_name);

	if (debug)
	{
	    elf_parser_free (elf);
	    elf = debug;
	    
	    seen_names = g_list_prepend (seen_names, fname);
	    fname = debug_name;
	}
    }
    while (debug);

    g_free (fname);
    g_list_foreach (seen_names, (GFunc)g_free, NULL);
    g_list_free (seen_names);
    
    return elf;
}

static GHashTable *bin_files;

BinFile *
bin_file_new (const char *filename)
{
    /* FIXME: should be able to return an error */
    BinFile *bf;

    if (!bin_files)
	bin_files = g_hash_table_new (g_str_hash, g_str_equal);

    bf = g_hash_table_lookup (bin_files, filename);

    if (bf)
    {
	bf->ref_count++;
    }
    else
    {
	bf = g_new0 (BinFile, 1);

	if (strcmp (filename, "[vdso]") == 0)
	{
	    gsize length;
	    const guint8 *vdso_bytes;

	    vdso_bytes = process_get_vdso_bytes (&length);
	    if (vdso_bytes)
	    {
		bf->elf = elf_parser_new_from_data (vdso_bytes, length);
#if 0
		g_print ("got vdso elf: %p (%d)\n", bf->elf, length);
#endif
	    }
	    else
		bf->elf = NULL;
	}
	else
	{
	    bf->elf = elf_parser_new (filename, NULL);
	}
	
	/* We need the text offset of the actual binary, not the
	 * (potential) debug binary
	 */
	if (bf->elf)
	{
	    bf->text_offset = elf_parser_get_text_offset (bf->elf);
#if 0
	    g_print ("text offset: %d\n", bf->text_offset);
#endif
	    
	    bf->elf = find_separate_debug_file (bf->elf, filename);
	}
	
	bf->inode = read_inode (filename);
	bf->filename = g_strdup (filename);
	bf->undefined_name = g_strdup_printf ("In file %s", filename);
	bf->ref_count = 1;
	
	g_hash_table_insert (bin_files, bf->filename, bf);
    }
    
    return bf;
}

void
bin_file_free (BinFile *bin_file)
{
    if (bin_file->ref_count-- == 0)
    {
	g_hash_table_remove (bin_files, bin_file->filename);
	
	if (bin_file->elf)
	    elf_parser_free (bin_file->elf);
	
	g_free (bin_file->filename);
	g_free (bin_file->undefined_name);
	g_free (bin_file);
    }
}

const BinSymbol *
bin_file_lookup_symbol (BinFile    *bin_file,
			gulong      address)
{
    if (bin_file->elf)
    {
#if 0
	g_print ("bin file lookup lookup %d\n", address);
#endif
	address -= bin_file->text_offset;
	
	const ElfSym *sym = elf_parser_lookup_symbol (bin_file->elf, address);

#if 0
	g_print ("lookup %d in %s\n", address, bin_file->filename);
#endif
	
	if (sym)
	{
#if 0
	    g_print ("found  %lx => %s\n", address, bin_symbol_get_name (bin_file, sym));
#endif
	    return (const BinSymbol *)sym;
	}
    }
#if 0
    else
	g_print ("no elf file for %s\n", bin_file->filename);
#endif

#if 0
    g_print ("%lx undefined in %s (textoffset %d)\n", address + bin_file->text_offset, bin_file->filename, bin_file->text_offset);
#endif
    
    return (const BinSymbol *)bin_file->undefined_name;
}

ino_t
bin_file_get_inode (BinFile    *bin_file)
{
    return bin_file->inode;
}

const char *
bin_symbol_get_name (BinFile *file, const BinSymbol *symbol)
{
    if (file->undefined_name == (char *)symbol)
	return file->undefined_name;
    else
	return elf_parser_get_sym_name (file->elf, (const ElfSym *)symbol);
}
