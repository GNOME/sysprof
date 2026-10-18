/* sysprof-source-location.c
 *
 * Copyright 2026 Christian Hergert <christian@sourceandstack.com>
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

#include "sysprof-source-location-private.h"

struct _SysprofSourceLocation
{
  GObject parent_instance;
  GFile *file;
  guint line;
  guint character;
};

G_DEFINE_FINAL_TYPE (SysprofSourceLocation, sysprof_source_location, G_TYPE_OBJECT)

static void
sysprof_source_location_finalize (GObject *object)
{
  SysprofSourceLocation *self = SYSPROF_SOURCE_LOCATION (object);

  g_clear_object (&self->file);

  G_OBJECT_CLASS (sysprof_source_location_parent_class)->finalize (object);
}

static void
sysprof_source_location_class_init (SysprofSourceLocationClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = sysprof_source_location_finalize;
}

static void
sysprof_source_location_init (SysprofSourceLocation *self)
{
}

SysprofSourceLocation *
_sysprof_source_location_new (GFile *file,
                              guint  line,
                              guint  character)
{
  SysprofSourceLocation *self;

  g_return_val_if_fail (G_IS_FILE (file), NULL);

  self = g_object_new (SYSPROF_TYPE_SOURCE_LOCATION, NULL);
  self->file = g_object_ref (file);
  self->line = line;
  self->character = character;

  return self;
}

/**
 * sysprof_source_location_dup_file:
 * @self: a #SysprofSourceLocation
 *
 * Returns: (transfer full): the source file
 */
GFile *
sysprof_source_location_dup_file (SysprofSourceLocation *self)
{
  g_return_val_if_fail (SYSPROF_IS_SOURCE_LOCATION (self), NULL);

  return g_object_ref (self->file);
}

/**
 * sysprof_source_location_get_line:
 * @self: a #SysprofSourceLocation
 *
 * Returns: the 1-based line, or 0 if unknown
 */
guint
sysprof_source_location_get_line (SysprofSourceLocation *self)
{
  g_return_val_if_fail (SYSPROF_IS_SOURCE_LOCATION (self), 0);

  return self->line;
}

/**
 * sysprof_source_location_get_character:
 * @self: a #SysprofSourceLocation
 *
 * Returns: the 1-based character offset, or 0 if unknown
 */
guint
sysprof_source_location_get_character (SysprofSourceLocation *self)
{
  g_return_val_if_fail (SYSPROF_IS_SOURCE_LOCATION (self), 0);

  return self->character;
}
