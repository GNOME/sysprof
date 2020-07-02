/* sysprof-capture-gobject.c
 *
 * Copyright 2019 Christian Hergert <chergert@redhat.com>
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

#include <errno.h>
#include <sysprof-capture.h>

#include "sysprof-capture-gobject.h"

G_DEFINE_BOXED_TYPE (SysprofCaptureReader, sysprof_capture_reader, (GBoxedCopyFunc)sysprof_capture_reader_ref, (GBoxedFreeFunc)sysprof_capture_reader_unref)
G_DEFINE_BOXED_TYPE (SysprofCaptureWriter, sysprof_capture_writer, (GBoxedCopyFunc)sysprof_capture_writer_ref, (GBoxedFreeFunc)sysprof_capture_writer_unref)
G_DEFINE_BOXED_TYPE (SysprofCaptureCursor, sysprof_capture_cursor, (GBoxedCopyFunc)sysprof_capture_cursor_ref, (GBoxedFreeFunc)sysprof_capture_cursor_unref)

SysprofCaptureReader *
sysprof_capture_reader_new_with_error (const char  *filename,
                                       GError     **error)
{
  SysprofCaptureReader *ret;

  if (!(ret = sysprof_capture_reader_new (filename)))
    g_set_error_literal (error,
                         G_FILE_ERROR,
                         g_file_error_from_errno (errno),
                         g_strerror (errno));

  return ret;
}

SysprofCaptureReader *
sysprof_capture_reader_new_from_fd_with_error (int      fd,
                                               GError **error)
{
  SysprofCaptureReader *ret;

  if (!(ret = sysprof_capture_reader_new_from_fd (fd)))
    g_set_error_literal (error,
                         G_FILE_ERROR,
                         g_file_error_from_errno (errno),
                         g_strerror (errno));

  return ret;
}

SysprofCaptureReader *
sysprof_capture_writer_create_reader_with_error (SysprofCaptureWriter  *self,
                                                 GError               **error)
{
  SysprofCaptureReader *ret;

  if (!(ret = sysprof_capture_writer_create_reader (self)))
    g_set_error_literal (error,
                         G_FILE_ERROR,
                         g_file_error_from_errno (errno),
                         g_strerror (errno));

  return ret;
}

bool
sysprof_capture_reader_save_as_with_error (SysprofCaptureReader  *self,
                                           const char            *filename,
                                           GError               **error)
{
  if (!sysprof_capture_reader_save_as (self, filename))
    {
      g_set_error_literal (error,
                           G_FILE_ERROR,
                           g_file_error_from_errno (errno),
                           g_strerror (errno));
      return false;
    }

  return true;
}
