/* sp-line-reader.c
 *
 * Copyright (C) 2015 Christian Hergert <christian@hergert.me>
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

#include <string.h>

#include "sp-line-reader.h"

struct _SpLineReader
{
  const gchar *contents;
  gsize        length;
  gsize        pos;
};

void
sp_line_reader_free (SpLineReader *self)
{
  g_slice_free (SpLineReader, self);
}

/**
 * sp_line_reader_new:
 * @contents: The buffer to read lines from
 * @length: the length of @buffer in bytes
 *
 * Creates a new #SpLineReader for the contents provided. @contents are not
 * copied and therefore it is a programming error to free contents before
 * freeing the #SpLineReader structure.
 *
 * Use sp_line_reader_next() to read through the lines of the buffer.
 *
 * Returns: (transfer full): A new #SpLineReader that should be freed with
 *   sp_line_reader_free() when no longer in use.
 */
SpLineReader *
sp_line_reader_new (const gchar *contents,
                    gssize       length)
{
  SpLineReader *self;

  g_return_val_if_fail (contents != NULL, NULL);

  self = g_slice_new (SpLineReader);

  if (length < 0)
    length = strlen (contents);

  if (contents != NULL)
    {
      self->contents = contents;
      self->length = length;
      self->pos = 0;
    }
  else
    {
      self->contents = NULL;
      self->length = 0;
      self->pos = 0;
    }

  return self;
}

/**
 * sp_line_reader_next:
 * @self: the #SpLineReader
 * @length: a location for the length of the line in bytes
 *
 * Moves forward to the beginning of the next line in the buffer. No changes to
 * the buffer are made, and the result is a pointer within the string passed as
 * @contents in sp_line_reader_init(). Since the line most likely will not be
 * terminated with a NULL byte, you must provide @length to determine the
 * length of the line.
 *
 * Using "line[length]" will place you on the \n that was found for the line.
 * However, to perform this safely, you need to know that your string was
 * either \0 terminated to begin with, or that your buffer provides enough space
 * to guarantee you can dereference past the last "textual content" of the
 * buffer.
 *
 * Returns: (nullable) (transfer none): The beginning of the line within the buffer
 */
const gchar *
sp_line_reader_next (SpLineReader *self,
                     gsize        *length)
{
  const gchar *ret;
  const gchar *endptr;

  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (length != NULL, NULL);

  if ((self->contents == NULL) || (self->pos >= self->length))
    {
      *length = 0;
      return NULL;
    }

  ret = &self->contents [self->pos];

  endptr = memchr (ret, '\n', self->length - self->pos);
  if (G_UNLIKELY (endptr == NULL))
    endptr = &self->contents [self->length];

  *length = (endptr - ret);
  self->pos += *length + 1;

  return ret;
}
