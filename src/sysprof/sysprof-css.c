/* sysprof-css.c
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

#include "sysprof-resources.h"

#include "sysprof-css-private.h"

void
_sysprof_css_init (void)
{
  static GtkCssProvider *css;

  if (css == NULL)
    {
      g_resources_register (sysprof_get_resource ());

      css = gtk_css_provider_new ();
      gtk_css_provider_load_from_resource (css, "/org/gnome/sysprof/style.css");
      gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                                  GTK_STYLE_PROVIDER (css),
                                                  G_MAXUINT);
    }
}
