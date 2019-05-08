/* main.c
 *
 * Copyright 2016 Christian Hergert <christian@hergert.me>
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
 */

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <sysprof.h>

#include "sp-application.h"

gint
main (gint argc,
      gchar *argv[])
{
  g_autoptr(SpApplication) app = NULL;
  gint ret;

  sp_clock_init ();

  app = sp_application_new ();
  ret = g_application_run (G_APPLICATION (app), argc, argv);

  return ret;
}
