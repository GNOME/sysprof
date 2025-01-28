/* sysprof-util.c
 *
 * Copyright 2023 Christian Hergert <chergert@redhat.com>
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

#include "sysprof-util.h"

static gboolean
hide_callback (GtkWidget     *widget,
               GdkFrameClock *frame_clock,
               gpointer       user_data)
{
  gint64 *begin_time = user_data;
  double fraction;

  g_assert (GTK_IS_WIDGET (widget));
  g_assert (begin_time != NULL);

  fraction = (g_get_monotonic_time () - *begin_time) / (double)G_USEC_PER_SEC;

  if (fraction > 1)
    {
      gtk_widget_set_visible (widget, FALSE);
      gtk_widget_set_opacity (widget, 1);
      return G_SOURCE_REMOVE;
    }

  if (fraction < 0)
    fraction = 0;

  gtk_widget_set_opacity (widget, 1.-fraction);

  return G_SOURCE_CONTINUE;
}

void
_gtk_widget_hide_with_fade (GtkWidget *widget)
{
  gint64 begin_time;

  g_return_if_fail (GTK_IS_WIDGET (widget));

  if (!gtk_widget_get_visible (widget))
    return;

  begin_time = g_get_monotonic_time ();
  gtk_widget_add_tick_callback (widget,
                                hide_callback,
                                g_memdup2 (&begin_time, sizeof begin_time),
                                g_free);
}

GFile *
_get_default_state_file (void)
{
  return g_file_new_build_filename (g_get_user_data_dir (), APP_ID_S, "recording-template.json", NULL);
}
