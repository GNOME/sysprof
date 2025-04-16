/* main.c
 *
 * Copyright 2016-2023 Christian Hergert <chergert@redhat.com>
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

#include <locale.h>
#include <signal.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <libdex.h>

#include <sysprof-capture.h>

#include "sysprof-application.h"

int
main (int argc,
      char *argv[])
{
  g_autoptr(SysprofApplication) app = NULL;
  gint ret;

  sysprof_clock_init ();

  /* Ignore SIGPIPE */
  signal (SIGPIPE, SIG_IGN);

  /* Set up gettext translations */
  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  g_set_prgname ("sysprof");

  dex_init ();

  /* Setup thread pool scheduler at startup */
  (void)dex_thread_pool_scheduler_get_default ();

  app = sysprof_application_new ();
  ret = g_application_run (G_APPLICATION (app), argc, argv);

  return ret;
}
