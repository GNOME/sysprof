/* sysprof-color-iter.c
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

#include "sysprof-color-iter-private.h"

static GdkRGBA *colors;
static guint n_colors;

void
sysprof_color_iter_init (SysprofColorIter *iter)
{
  if (g_once_init_enter (&colors))
    {
      static const char *color_strings[] = {
        "#3584e4",
        "#ff7800",
        "#33d17a",
        "#9141ac",
        "#e5a50a",
        "#63452c",
        "#ffbe6f",
        "#613583",
        "#9a9996",
        "#ed333b",
        "#1a5fb4",
        "#26a269",
        "#c061cb",
        "#c64600",
      };
      GdkRGBA *_colors;

      n_colors = G_N_ELEMENTS (color_strings);
      _colors = g_new0 (GdkRGBA, n_colors);

      for (guint i = 0; i < n_colors; i++)
        gdk_rgba_parse (&_colors[i], color_strings[i]);

      g_once_init_leave (&colors, _colors);
    }

  iter->colors = colors;
  iter->n_colors = n_colors;
  iter->position = 0;
}

const GdkRGBA *
sysprof_color_iter_next (SysprofColorIter *iter)
{
  const GdkRGBA *ret;

  g_assert (iter->colors != NULL);
  g_assert (iter->n_colors > 0);

  ret = &iter->colors[iter->position % iter->n_colors];

  iter->position++;

  return ret;
}
