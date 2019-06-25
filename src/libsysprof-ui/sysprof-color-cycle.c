/* sysprof-color-cycle.c
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sysprof-color-cycle"

#include "config.h"

#include "sysprof-color-cycle.h"

G_DEFINE_BOXED_TYPE (SysprofColorCycle, sysprof_color_cycle, sysprof_color_cycle_ref, sysprof_color_cycle_unref)

static const gchar *default_colors[] = {

  "#1a5fb4", /* Blue 5 */
  "#26a269", /* Green 5 */
  "#e5a50a", /* Yellow 5 */
  "#c64600", /* Orange 5 */
  "#a51d2d", /* Red 5 */
  "#613583", /* Purple 5 */
  "#63452c", /* Brown 5 */

  "#1c71d8", /* Blue 4 */
  "#2ec27e", /* Green 4 */
  "#f5c211", /* Yellow 4 */
  "#e66100", /* Orange 4 */
  "#c01c28", /* Red 4 */
  "#813d9c", /* Purple 4 */
  "#865e3c", /* Brown 4 */

  "#3584e4", /* Blue 3 */
  "#33d17a", /* Green 3 */
  "#f6d32d", /* Yellow 3 */
  "#ff7800", /* Orange 3 */
  "#e01b24", /* Red 3 */
  "#9141ac", /* Purple 3 */
  "#986a44", /* Brown 3 */

  "#62a0ea", /* Blue 2 */
  "#57e389", /* Green 2 */
  "#f8e45c", /* Yellow 2 */
  "#ffa348", /* Orange 2 */
  "#ed333b", /* Red 2 */
  "#c061cb", /* Purple 2 */
  "#b5835a", /* Brown 2 */

  "#99c1f1", /* Blue 1 */
  "#8ff0a4", /* Green 1 */
  "#f9f06b", /* Yellow 1 */
  "#ffbe6f", /* Orange 1 */
  "#f66151", /* Red 1 */
  "#dc8add", /* Purple 1 */
  "#cdab8f", /* Brown 1 */

  NULL
};

struct _SysprofColorCycle
{
  volatile gint ref_count;
  GdkRGBA *colors;
  gsize n_colors;
  guint position;
};

static void
sysprof_color_cycle_destroy (SysprofColorCycle *self)
{
  g_free (self->colors);
  g_slice_free (SysprofColorCycle, self);
}

SysprofColorCycle *
sysprof_color_cycle_new (void)
{
  SysprofColorCycle *self;

  self = g_slice_new0 (SysprofColorCycle);
  self->ref_count = 1;
  self->n_colors = g_strv_length ((gchar **)default_colors);
  self->colors = g_new0 (GdkRGBA, self->n_colors);

  for (guint i = 0; default_colors[i]; i++)
    {
      if G_UNLIKELY (!gdk_rgba_parse (&self->colors[i], default_colors[i]))
        g_warning ("Failed to parse color %s into an RGBA", default_colors[i]);
    }

  return self;
}

SysprofColorCycle *
sysprof_color_cycle_ref (SysprofColorCycle *self)
{
  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (self->ref_count > 0, NULL);
  g_atomic_int_inc (&self->ref_count);
  return self;
}

void
sysprof_color_cycle_unref (SysprofColorCycle *self)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (self->ref_count > 0);
  if (g_atomic_int_dec_and_test (&self->ref_count))
    sysprof_color_cycle_destroy (self);
}

void
sysprof_color_cycle_next (SysprofColorCycle *self,
                          GdkRGBA      *rgba)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (self->position < self->n_colors);

  *rgba = self->colors[self->position];

  /*
   * TODO: Adjust color HSV/etc
   *
   * We could simply adjust the brightness/etc after we dispatch
   * a color so that we get darker as we go.
   */

  self->position = (self->position + 1) % self->n_colors;
}

void
sysprof_color_cycle_reset (SysprofColorCycle *self)
{
  g_return_if_fail (self != NULL);

  for (guint i = 0; default_colors[i]; i++)
    {
      if G_UNLIKELY (!gdk_rgba_parse (&self->colors[i], default_colors[i]))
        g_warning ("Failed to parse color %s into an RGBA", default_colors[i]);
    }

  self->position = 0;
}
