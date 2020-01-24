/* MemProf -- memory profiler and leak detector
 * Copyright 1999, 2000, 2001, Red Hat, Inc.
 * Copyright 2002, Kristian Rietveld
 *
 * Sysprof -- Sampling, systemwide CPU profiler
 * Copyright 2004, 2005, 2006, 2007, Soeren Sandmann
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
#include <stdint.h>

#include "binfile.h"
#include "elfparser.h"

struct bin_file_t
{
    int         ref_count;

    GList *     elf_files;

    char *      filename;

    char *      undefined_name;

    gulong      text_offset;

    gboolean    inode_check;
    ino_t       inode;
};

static ino_t
read_inode (const char *filename)
{
    struct stat statbuf;

    if (strcmp (filename, "[vdso]") == 0)
        return (ino_t)0;

    if (stat (filename, &statbuf) < 0)
        return (ino_t)-1;

    return statbuf.st_ino;
}

static gboolean
already_warned (const char *name)
{
    static GPtrArray *warnings;
    guint i;

    if (!warnings)
        warnings = g_ptr_array_new ();

    for (i = 0; i < warnings->len; ++i)
    {
        if (strcmp (warnings->pdata[i], name) == 0)
            return TRUE;
    }

    g_ptr_array_add (warnings, g_strdup (name));

    return FALSE;
}

static ElfParser *
get_build_id_file (ElfParser *elf)
{
    const char *build_id;
    GList *tries = NULL, *list;
    char *init, *rest;
    ElfParser *result = NULL;
    char *tmp;

    build_id = elf_parser_get_build_id (elf);

    if (!build_id)
        return NULL;

    if (strlen (build_id) < 4)
        return NULL;

    init = g_strndup (build_id, 2);
    rest = g_strdup_printf ("%s%s", build_id + 2, ".debug");

    tmp = g_build_filename (
        "/usr", "lib", "debug", ".build-id", init, rest, NULL);
    tries = g_list_append (tries, tmp);

    tmp = g_build_filename (DEBUGDIR, ".build-id", init, rest, NULL);
    tries = g_list_append (tries, tmp);

    for (list = tries; list != NULL; list = list->next)
    {
        char *name = list->data;
        ElfParser *parser = elf_parser_new (name, NULL);

        if (parser)
        {
            const char *file_id = elf_parser_get_build_id (parser);

            if (file_id && strcmp (build_id, file_id) == 0)
            {
                result = parser;
                break;
            }

            elf_parser_free (parser);
        }
    }

    g_list_foreach (tries, (GFunc)g_free, NULL);
    g_list_free (tries);

    g_free (init);
    g_free (rest);

    return result;
}

static ElfParser *
get_debuglink_file (ElfParser            *elf,
                    const char           *filename,
                    char                **new_name,
                    const gchar * const  *debug_dirs)
{
    const char *basename;
    char *dir;
    guint32 crc32;
    ElfParser *result = NULL;
    const char *build_id;
    guint i;

    if (!elf)
        return NULL;

    basename = elf_parser_get_debug_link (elf, &crc32);

    build_id = elf_parser_get_build_id (elf);

#if 0
    g_print ("   debug link for %s is %s\n", filename, basename);
#endif

    if (!basename)
        return NULL;

    dir = g_path_get_dirname (filename);

    for (i = 0; debug_dirs[i]; i++)
    {
        const char *name = debug_dirs[i];
        ElfParser *parser = elf_parser_new (name, NULL);
        guint32 file_crc;
        const char *file_build_id;

        if (parser)
        {
            /* If both files have build ids, and they don't match,
             * there is no point computing a CRC32 that we know
             * will fail
             */
            file_build_id = elf_parser_get_build_id (parser);
            if (build_id && file_build_id && strcmp (build_id, file_build_id) != 0)
                goto skip;

            file_crc = elf_parser_get_crc32 (parser);

            if (file_crc == crc32)
            {
                result = parser;
                *new_name = g_strdup (name);
                break;
            }
            else
            {
                if (!already_warned (name))
                {
                    g_print ("warning: %s has wrong crc %x, %s has crc %x)\n",
                             name, file_crc, filename, crc32);
                }
            }

        skip:
            elf_parser_free (parser);
        }
    }

    g_free (dir);

    return result;
}

static GList *
get_debug_binaries (GList               *files,
                    ElfParser           *elf,
                    const char          *filename,
                    const gchar * const *debug_dirs)
{
    ElfParser *build_id_file;
    GHashTable *seen_names;
    GList *free_us = NULL;

    build_id_file = get_build_id_file (elf);

    if (build_id_file)
        return g_list_prepend (files, build_id_file);

    /* .gnu_debuglink is actually a chain of debuglinks, and
     * there have been real-world cases where following it was
     * necessary to get useful debug information.
     */
    seen_names = g_hash_table_new (g_str_hash, g_str_equal);

    while (elf)
    {
        char *debug_name;

        if (g_hash_table_lookup (seen_names, filename))
            break;

        g_hash_table_insert (seen_names, (char *)filename, (char *)filename);

        elf = get_debuglink_file (elf, filename, &debug_name, debug_dirs);

        if (elf)
        {
            files = g_list_prepend (files, elf);
            free_us = g_list_prepend (free_us, debug_name);
            filename = debug_name;
        }
    }

    g_list_foreach (free_us, (GFunc)g_free, NULL);
    g_list_free (free_us);

    g_hash_table_destroy (seen_names);

    return files;
}

static char **
get_lines (const char *format,
           ...)
  G_GNUC_PRINTF (1, 2);

#ifdef __linux__
static char **
get_lines (const char *format,
           ...)
{
    va_list args;
    char *filename;
    char **result = NULL;
    char *contents;

    va_start (args, format);
    filename = g_strdup_vprintf (format, args);
    va_end (args);

    if (g_file_get_contents (filename, &contents, NULL, NULL))
    {
        result = g_strsplit (contents, "\n", -1);

        g_free (contents);
    }

    g_free (filename);

    return result;
}
#endif

static const uint8_t *
get_vdso_bytes (size_t *length)
{
#ifdef __linux__
    static const uint8_t *bytes = NULL;
    static size_t n_bytes = 0;
    static gboolean has_data;

    if (!has_data)
    {
        char **lines = get_lines ("/proc/%d/maps", getpid());
        int i;

        for (i = 0; lines[i] != NULL; ++i)
        {
            char file[256];
            gulong start;
            gulong end;
            int count = sscanf (
                lines[i], "%lx-%lx %*15s %*x %*x:%*x %*u %255s",
                &start, &end, file);

            if (count == 3 && strcmp (file, "[vdso]") == 0)
            {
                n_bytes = end - start;

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
                bytes = g_memdup ((uint8_t *)start, n_bytes);

                has_data = TRUE;
            }
        }
    }

    if (length)
        *length = n_bytes;

    return bytes;
#else
    if (length)
        *length = 0;
    return NULL;
#endif
}

bin_file_t *
bin_file_new (const char          *filename,
              const gchar * const *debug_dirs)
{
    const gchar *real_filename = filename;
    ElfParser *elf = NULL;
    bin_file_t *bf;

    if (g_str_has_prefix (filename, "/var/run/host/"))
      real_filename = filename + strlen ("/var/run/host");

    bf = g_new0 (bin_file_t, 1);

    bf->inode_check = FALSE;
    bf->filename = g_strdup (filename);
    bf->undefined_name = g_strdup_printf ("In file %s", real_filename);
    bf->ref_count = 1;
    bf->elf_files = NULL;

    if (strcmp (filename, "[vdso]") == 0)
    {
        const guint8 *vdso_bytes;
        gsize length;

        vdso_bytes = get_vdso_bytes (&length);

        if (vdso_bytes)
            elf = elf_parser_new_from_data (vdso_bytes, length);
    }
    else
    {
        elf = elf_parser_new (filename, NULL);
    }

    if (elf)
    {
        /* We need the text offset of the actual binary, not the
         * (potential) debug binaries
         */
        bf->text_offset = elf_parser_get_text_offset (elf);

        bf->elf_files = get_debug_binaries (bf->elf_files, elf, filename, debug_dirs);
        bf->elf_files = g_list_append (bf->elf_files, elf);

        bf->inode = read_inode (filename);
    }

    return bf;
}

void
bin_file_free (bin_file_t *bin_file)
{
    if (--bin_file->ref_count == 0)
    {
        g_list_foreach (bin_file->elf_files, (GFunc)elf_parser_free, NULL);
        g_list_free (bin_file->elf_files);

        g_free (bin_file->filename);
        g_free (bin_file->undefined_name);
        g_free (bin_file);
    }
}

const bin_symbol_t *
bin_file_lookup_symbol (bin_file_t *bin_file,
                        gulong      address)
{
    GList *list;

#if 0
    g_print ("-=-=-=- \n");

    g_print ("bin file lookup lookup %d\n", address);
#endif

    address -= bin_file->text_offset;

#if 0
    g_print ("lookup %d in %s\n", address, bin_file->filename);
#endif

    for (list = bin_file->elf_files; list != NULL; list = list->next)
    {
        ElfParser *elf = list->data;
        const ElfSym *sym = elf_parser_lookup_symbol (elf, address);

        if (sym)
        {
#if 0
            g_print ("found  %lx => %s\n", address,
                     bin_symbol_get_name (bin_file, sym));
#endif
            return (const bin_symbol_t *)sym;
        }
    }

#if 0
    g_print ("%lx undefined in %s (textoffset %x)\n",
             address + bin_file->text_offset,
             bin_file->filename,
             bin_file->text_offset);
#endif

    return (const bin_symbol_t *)bin_file->undefined_name;
}

gboolean
bin_file_check_inode (bin_file_t *bin_file,
                      ino_t       inode)
{
    if (bin_file->inode == inode)
        return TRUE;

    if (!bin_file->elf_files)
        return FALSE;

    if (!bin_file->inode_check)
    {
        g_print ("warning: Inode mismatch for %s (disk: %"G_GUINT64_FORMAT", memory: %"G_GUINT64_FORMAT")\n",
                 bin_file->filename, (guint64)bin_file->inode, (guint64)inode);

        bin_file->inode_check = TRUE;
    }

    return FALSE;
}

static const ElfSym *
get_elf_sym (bin_file_t *file,
             const bin_symbol_t *symbol,
             ElfParser **elf_ret)
{
    GList *list;

    for (list = file->elf_files; list != NULL; list = list->next)
    {
        const ElfSym *sym = (const ElfSym *)symbol;
        ElfParser *elf = list->data;

        if (elf_parser_owns_symbol (elf, sym))
        {
            *elf_ret = elf;
            return sym;
        }
    }

    g_critical ("Internal error: unrecognized symbol pointer");

    *elf_ret = NULL;
    return NULL;
}

const char *
bin_symbol_get_name (bin_file_t *file,
                     const bin_symbol_t *symbol)
{
    if (file->undefined_name == (char *)symbol)
    {
        return file->undefined_name;
    }
    else
    {
        ElfParser *elf;
        const ElfSym *sym;

        sym = get_elf_sym (file, symbol, &elf);

        return elf_parser_get_sym_name (elf, sym);
    }
}

gulong
bin_symbol_get_address (bin_file_t         *file,
                        const bin_symbol_t *symbol)
{
    if (file->undefined_name == (char *)symbol)
    {
        return 0x0;
    }
    else
    {
        ElfParser *elf;
        const ElfSym *sym;

        sym = get_elf_sym (file, symbol, &elf);

        return elf_parser_get_sym_address (elf, sym);
    }
}

void
bin_symbol_get_address_range (bin_file_t         *file,
                              const bin_symbol_t *symbol,
                              gulong             *begin,
                              gulong             *end)
{
    if (file->undefined_name == (char *)symbol)
    {
        *begin = 0;
        *end = 0;
    }
    else
    {
        ElfParser *elf;
        const ElfSym *sym;

        sym = get_elf_sym (file, symbol, &elf);
        elf_parser_get_sym_address_range (elf, sym, begin, end);
    }
}
