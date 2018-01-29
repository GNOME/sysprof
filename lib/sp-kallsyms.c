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
sp_kallsyms_new (const gchar *path)
{
  g_autoptr(SpKallsyms) self = NULL;

  if (path == NULL)
    path = "/proc/kallsyms";

  self = g_slice_new0 (SpKallsyms);

  if (!g_file_get_contents (path, &self->buf, &self->buflen, NULL))
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
  char *pptr;

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

  /* We'll keep going if we fail to parse, (null) usually, so that we
   * just skip to the next line.
   */
  addr = g_ascii_strtoull (tok, &pptr, 16);
  if ((pptr == tok) || (addr == G_MAXUINT64 && errno == ERANGE) || (addr == 0 && errno == EINVAL))
    addr = 0;

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

  if (addr == 0)
    goto try_next;

  *name = tok;

  return TRUE;
}
