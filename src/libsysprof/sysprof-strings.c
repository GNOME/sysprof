/* sysprof-strings.c
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

#include "sysprof-strings-private.h"

struct _SysprofStrings
{
  GRWLock     rwlock;
  GHashTable *hashtable;
};

SysprofStrings *
sysprof_strings_new (void)
{
  SysprofStrings *self;

  self = g_atomic_rc_box_new0 (SysprofStrings);
  g_rw_lock_init (&self->rwlock);
  self->hashtable = g_hash_table_new_full (g_str_hash,
                                           g_str_equal,
                                           (GDestroyNotify)g_ref_string_release,
                                           NULL);

  return self;
}

SysprofStrings *
sysprof_strings_ref (SysprofStrings *self)
{
  return g_atomic_rc_box_acquire (self);
}

static void
sysprof_strings_finalize (gpointer data)
{
  SysprofStrings *self = data;

  g_rw_lock_clear (&self->rwlock);
  g_clear_pointer (&self->hashtable, g_hash_table_unref);
}

void
sysprof_strings_unref (SysprofStrings *self)
{
  g_atomic_rc_box_release_full (self, sysprof_strings_finalize);
}

GRefString *
sysprof_strings_get (SysprofStrings *self,
                     const char     *string)
{
  GRefString *ret;

  if (string == NULL)
    return NULL;

  g_rw_lock_reader_lock (&self->rwlock);
  if (!(ret = g_hash_table_lookup (self->hashtable, string)))
    {
      g_rw_lock_reader_unlock (&self->rwlock);
      g_rw_lock_writer_lock (&self->rwlock);

      /* Check again, now with write lock */
      if (!(ret = g_hash_table_lookup (self->hashtable, string)))
        {
          ret = g_ref_string_new (string);
          g_hash_table_insert (self->hashtable, ret, ret);
        }

      g_rw_lock_writer_unlock (&self->rwlock);

      return g_ref_string_acquire (ret);
    }
  g_rw_lock_reader_unlock (&self->rwlock);

  return g_ref_string_acquire (ret);
}
