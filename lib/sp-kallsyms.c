/* sp-kallsyms.c
 *
 * Copyright © 2018 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sp-kallsyms"

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "sp-kallsyms.h"

struct _SpKallsyms
{
  gchar *buf;
  gsize  buflen;
  gchar *endptr;
  gchar *iter;
};

void
sp_kallsyms_free (SpKallsyms *self)
{
  if (self != NULL)
    {
      g_clear_pointer (&self->buf, g_free);
      g_slice_free (SpKallsyms, self);
    }
}

SpKallsyms *
sp_kallsyms_new (void)
{
  g_autoptr(SpKallsyms) self = NULL;

  self = g_slice_new0 (SpKallsyms);

  if (!g_file_get_contents ("/proc/kallsyms", &self->buf, &self->buflen, NULL))
    return NULL;

  self->iter = self->buf;
  self->endptr = self->buf + self->buflen;

  return g_steal_pointer (&self);
}

gboolean
sp_kallsyms_next (SpKallsyms   *self,
                  const gchar **name,
                  guint64      *address,
                  guint8       *type)
{
  guint64 addr;
  char *tok;

  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (self->buf != NULL, FALSE);
  g_return_val_if_fail (self->buflen > 0, FALSE);
  g_return_val_if_fail (self->iter != NULL, FALSE);
  g_return_val_if_fail (self->endptr != NULL, FALSE);

try_next:

  if (self->iter >= self->endptr)
    return FALSE;

  tok = strtok_r (self->iter, " \t\n", &self->iter);
  if (!tok || *tok == 0)
    return FALSE;

  if (*tok == '[')
    {
      tok = strtok_r (self->iter, " \t\n", &self->iter);
      if (!tok || *tok == 0)
        return FALSE;
    }

  addr = g_ascii_strtoull (tok, NULL, 10);
  if ((addr == G_MAXUINT64 && errno == ERANGE) ||
      (addr == 0 && errno == EINVAL))
    return FALSE;

  *address = addr;

  if (self->iter >= self->endptr)
    return FALSE;

  tok = strtok_r (self->iter, " \t\n", &self->iter);
  if (!tok || *tok == 0)
    return FALSE;

  switch (*tok)
    {
    case 'A':
    case 'B':
    case 'D':
    case 'R':
    case 'T':
    case 'V':
    case 'W':
    case 'a':
    case 'b':
    case 'd':
    case 'r':
    case 't':
    case 'w':
      *type = *tok;
      break;

    default:
      return FALSE;
    }

  if (self->iter >= self->endptr)
    return FALSE;

  tok = strtok_r (self->iter, " \t\n", &self->iter);
  if (!tok || *tok == 0)
    return FALSE;

  if (self->iter >= self->endptr)
    return FALSE;

  if (g_strcmp0 (tok, "(null)") == 0)
    goto try_next;

  *name = tok;

  return TRUE;
}
