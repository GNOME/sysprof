/* test-capture-view.c
 *
 * Copyright 2019 Christian Hergert <chergert@redhat.com>
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

#include <sysprof-ui.h>

gint
main (gint argc,
      gchar *argv[])
{
  GtkWindow *window;
  SysprofDisplay *view;
  SysprofCaptureReader *reader;
  g_autoptr(GError) error = NULL;

  gtk_init (&argc, &argv);

  if (argc != 2)
    {
      g_printerr ("usage: %s CAPTURE.syscap\n", argv[0]);
      return 1;
    }

  if (!(reader = sysprof_capture_reader_new_with_error (argv[1], &error)))
    {
      g_printerr ("Failed to load reader: %s\n", error->message);
      return 1;
    }

  window = g_object_new (GTK_TYPE_WINDOW,
                         "title", "SysprofDisplay",
                         "default-width", 800,
                         "default-height", 600,
                         NULL);
  view = g_object_new (SYSPROF_TYPE_DISPLAY,
                       "visible", TRUE,
                       NULL);
  gtk_container_add (GTK_CONTAINER (window), GTK_WIDGET (view));

  sysprof_display_load_async (view, reader, NULL, NULL, NULL);

  g_signal_connect (window, "delete-event", gtk_main_quit, NULL);
  gtk_window_present (GTK_WINDOW (window));
  gtk_main ();

  return 0;
}
