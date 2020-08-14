/* memory-stack-stash.c
 *
 * Copyright 2020 Christian Hergert <chergert@redhat.com>
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

#include <errno.h>
#include <glib/gi18n.h>
#include <locale.h>
#include <stddef.h>
#include <rax.h>
#include <sysprof.h>

#include "../stackstash.h"
#include "../stackstash.c"

static void
memory_stack_stash (SysprofCaptureReader *reader)
{
  SysprofCaptureFrameType type;
  StackStash *stash = stack_stash_new (NULL);

  while (sysprof_capture_reader_peek_type (reader, &type))
    {
      if (type == SYSPROF_CAPTURE_FRAME_ALLOCATION)
        {
          const SysprofCaptureAllocation *ev = sysprof_capture_reader_read_allocation (reader);

          if (ev == NULL)
            break;

          if (ev->alloc_size > 0)
            stack_stash_add_trace (stash, ev->addrs, ev->n_addrs, ev->alloc_size);
        }
      else
        {
          if (!sysprof_capture_reader_skip (reader))
            break;
        }
    }

  stack_stash_unref (stash);
}

gint
main (gint   argc,
      gchar *argv[])
{
  SysprofCaptureReader *reader;
  const gchar *filename = argv[1];

  if (argc < 2)
    {
      g_printerr ("usage: %s FILENAME\n", argv[0]);
      return EXIT_FAILURE;
    }

  /* Set up gettext translations */
  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  if (!(reader = sysprof_capture_reader_new (filename)))
    {
      int errsv = errno;
      g_printerr ("%s\n", g_strerror (errsv));
      return EXIT_FAILURE;
    }

  memory_stack_stash (reader);

  sysprof_capture_reader_unref (reader);

  return EXIT_SUCCESS;
}
