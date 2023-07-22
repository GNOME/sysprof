/* line-reader-private.h
 *
 * Copyright 2015-2023 Christian Hergert <christian@hergert.me>
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
#include <string.h>

G_BEGIN_DECLS

typedef struct _LineReader
{
  char   *contents;
  gsize   length;
  gssize  pos;
} LineReader;

static inline void
line_reader_init (LineReader *reader,
                  char       *contents,
                  gssize      length)
{
  g_assert (reader != NULL);

  if (length < 0)
    length = strlen (contents);

  if (contents != NULL)
    {
      reader->contents = contents;
      reader->length = length;
      reader->pos = 0;
    }
  else
    {
      reader->contents = NULL;
      reader->length = 0;
      reader->pos = 0;
    }
}

static inline char *
line_reader_next (LineReader *reader,
                  gsize      *length)
{
  char *ret = NULL;

  g_assert (reader != NULL);
  g_assert (length != NULL);

  if ((reader->contents == NULL) || (reader->pos >= reader->length))
    {
      *length = 0;
      return NULL;
    }

  ret = &reader->contents [reader->pos];

  for (; reader->pos < reader->length; reader->pos++)
    {
      if (reader->contents [reader->pos] == '\n')
        {
          *length = &reader->contents [reader->pos] - ret;
          /* Ingore the \r in \r\n if provided */
          if (*length > 0 && reader->pos > 0 && reader->contents [reader->pos - 1] == '\r')
            (*length)--;
          reader->pos++;
          return ret;
        }
    }

  *length = &reader->contents [reader->pos] - ret;

  return ret;
}

G_END_DECLS
