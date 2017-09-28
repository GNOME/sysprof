/* sp-color-cycle.c
 *
 * Copyright (C) 2016 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sp-color-cycle"

#include "util/sp-color-cycle.h"

G_DEFINE_BOXED_TYPE (SpColorCycle, sp_color_cycle, sp_color_cycle_ref, sp_color_cycle_unref)

static const gchar *default_colors[] = {
  "#73d216",
  "#f57900",
  "#3465a4",
  "#ef2929",
  "#75507b",
  "#ce5c00",
  "#c17d11",
  "#cc0000",
  "#edd400",
  "#555753",
  "#4e9a06",
  "#204a87",
  "#5c3566",
  "#a40000",
  "#c4a000",
  "#8f5902",
  "#2e3436",
  "#8ae234",
  "#729fcf",
  "#ad7fa8",
  "#fce94f",
  "#fcaf3e",
  "#e9b96e",
  "#888a85",
  NULL
};

struct _SpColorCycle
{
  volatile gint ref_count;
  GdkRGBA *colors;
  gsize n_colors;
  guint position;
};

static void
sp_color_cycle_destroy (SpColorCycle *self)
{
  g_free (self->colors);
  g_slice_free (SpColorCycle, self);
}

SpColorCycle *
sp_color_cycle_new (void)
{
  SpColorCycle *self;

  self = g_slice_new0 (SpColorCycle);
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

SpColorCycle *
sp_color_cycle_ref (SpColorCycle *self)
{
  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (self->ref_count > 0, NULL);
  g_atomic_int_inc (&self->ref_count);
  return self;
}

void
sp_color_cycle_unref (SpColorCycle *self)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (self->ref_count > 0);
  if (g_atomic_int_dec_and_test (&self->ref_count))
    sp_color_cycle_destroy (self);
}

void
sp_color_cycle_next (SpColorCycle *self,
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
sp_color_cycle_reset (SpColorCycle *self)
{
  g_return_if_fail (self != NULL);

  for (guint i = 0; default_colors[i]; i++)
    {
      if G_UNLIKELY (!gdk_rgba_parse (&self->colors[i], default_colors[i]))
        g_warning ("Failed to parse color %s into an RGBA", default_colors[i]);
    }

  self->position = 0;
}
