/* list-maps.c
 *
 * Copyright 2021 Christian Hergert <chergert@redhat.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"

#include <sysprof.h>
#include <glib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "sysprof-elf-symbol-resolver-private.h"

static ino_t
read_inode (const char *filename)
{
    struct stat statbuf;

    if (filename == NULL)
      return (ino_t)-1;

    if (strcmp (filename, "[vdso]") == 0)
        return (ino_t)0;

    if (stat (filename, &statbuf) < 0)
        return (ino_t)-1;

    return statbuf.st_ino;
}

static void
list_maps (const char *filename)
{
  g_autoptr(SysprofCaptureReader) reader = NULL;
  g_autoptr(SysprofSymbolResolver) resolver = NULL;
  SysprofCaptureFrameType type;

  if (!(reader = sysprof_capture_reader_new (filename)))
    return;

  resolver = sysprof_elf_symbol_resolver_new ();
  sysprof_symbol_resolver_load (resolver, reader);
  sysprof_capture_reader_reset (reader);

  while (sysprof_capture_reader_peek_type (reader, &type))
    {
      if (type == SYSPROF_CAPTURE_FRAME_MAP)
        {
          const SysprofCaptureMap *ev = sysprof_capture_reader_read_map (reader);
          g_autofree char *resolved = _sysprof_elf_symbol_resolver_resolve_path (SYSPROF_ELF_SYMBOL_RESOLVER (resolver), ev->frame.pid, ev->filename);
          const char *kind = _sysprof_elf_symbol_resolver_get_pid_kind (SYSPROF_ELF_SYMBOL_RESOLVER (resolver), ev->frame.pid);
          ino_t inode = read_inode (resolved);

          g_print ("PID %u (%s): ", ev->frame.pid, kind);
          g_print ("%s => %s", ev->filename, resolved ? resolved : "(NULL)");

          if (inode == (ino_t)-1)
            g_print (" (missing)");
          else if (inode != ev->inode)
            g_print (" (Inode mismatch, expected %lu got %lu)",
                     (gulong)ev->inode, (gulong)inode);

          g_print ("\n");
        }
      else
        {
          if (!sysprof_capture_reader_skip (reader))
            break;
        }
    }
}

int
main (int argc,
      char *argv[])
{
  if (argc == 1)
    {
      g_printerr ("usage: %s CAPTURE_FILE...\n", argv[0]);
      return 0;
    }

  for (guint i = 1; i < argc; i++)
    list_maps (argv[i]);

  return 0;
}
