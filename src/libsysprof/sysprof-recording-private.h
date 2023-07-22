/* sysprof-recording-private.h
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

#include <libdex.h>

#include "sysprof-instrument.h"
#include "sysprof-recording.h"
#include "sysprof-spawnable.h"

G_BEGIN_DECLS

SysprofRecording     *_sysprof_recording_new           (SysprofCaptureWriter  *writer,
                                                        SysprofSpawnable      *spawnable,
                                                        SysprofInstrument    **instruments,
                                                        guint                  n_instruments);
void                  _sysprof_recording_start         (SysprofRecording      *self);
SysprofCaptureWriter *_sysprof_recording_writer        (SysprofRecording      *self);
SysprofSpawnable     *_sysprof_recording_get_spawnable (SysprofRecording      *self);
DexFuture            *_sysprof_recording_add_file      (SysprofRecording      *self,
                                                        const char            *path,
                                                        gboolean               compress);
void                  _sysprof_recording_add_file_data (SysprofRecording      *self,
                                                        const char            *path,
                                                        const char            *contents,
                                                        gssize                 length,
                                                        gboolean               compress);
void                  _sysprof_recording_diagnostic    (SysprofRecording      *self,
                                                        const char            *domain,
                                                        const char            *format,
                                                        ...) G_GNUC_PRINTF (3, 4);
void                  _sysprof_recording_error         (SysprofRecording      *self,
                                                        const char            *domain,
                                                        const char            *format,
                                                        ...) G_GNUC_PRINTF (3, 4);

G_END_DECLS
