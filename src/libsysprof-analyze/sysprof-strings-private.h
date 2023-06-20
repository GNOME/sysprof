/* sysprof-strings-private.h
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

#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct _SysprofStrings SysprofStrings;

SysprofStrings *sysprof_strings_new    (void);
SysprofStrings *sysprof_strings_ref    (SysprofStrings *self);
void            sysprof_strings_unref  (SysprofStrings *self);
GRefString     *sysprof_strings_get    (SysprofStrings *self,
                                        const char     *string);

#define SYSPROF_STRV_INIT(...) ((const char * const[]){__VA_ARGS__,NULL})

static inline gboolean
sysprof_set_strv (char               ***dest,
                  const char * const   *src)
{
  if ((const char * const *)*dest == src)
    return FALSE;

  if (*dest == NULL ||
      src == NULL ||
      !g_strv_equal ((const char * const *)*dest, src))
    {
      char **copy = g_strdupv ((char **)src);
      g_strfreev (*dest);
      *dest = copy;
      return TRUE;
    }

  return FALSE;
}

G_END_DECLS
