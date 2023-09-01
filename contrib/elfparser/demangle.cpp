/* demangle.cpp
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

#include <cxxabi.h>
#include <stdlib.h>

#include "demangle.h"

char *
sysprof_cplus_demangle (const char *name)
{
  char *real_name;
  char *ret;
  int status;
  guint i;
  guint j;

  if (name == NULL)
    return NULL;

  real_name = abi::__cxa_demangle (name, 0, 0, &status);

  if (real_name == NULL)
    return NULL;

  /* We need to return a string that is guaranteed it can be freed with
   * g_free() rather than free(), so we might as well look for Legacy
   * Rust mangling like '$LT$' and '$GT$' while we're at it.
   */

  ret = (char *)g_malloc (strlen (real_name) + 1);

  for (i = 0, j = 0; real_name[i]; i++)
    {
      if (real_name[i] == '$')
        {
          if (real_name[i+1] == 'L' && real_name[i+2] == 'T' && real_name[i+3] == '$')
            ret[j++] = '<', i += 3;
          else if (real_name[i+1] == 'G' && real_name[i+2] == 'T' && real_name[i+3] == '$')
            ret[j++] = '>', i += 3;
        }
      else
        {
          ret[j++] = real_name[i];
        }
    }

  ret[j] = 0;

  free (real_name);

  return ret;
}
