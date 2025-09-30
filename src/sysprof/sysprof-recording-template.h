/* sysprof-recording-template.h
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

#include <sysprof.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_RECORDING_TEMPLATE (sysprof_recording_template_get_type())
#define SYSPROF_RECORDING_TEMPLATE_ERROR (sysprof_recording_template_error_quark())

typedef enum
{
  SYSPROF_RECORDING_TEMPLATE_ERROR_COMMAND_LINE = 1,
} SysprofRecordingTemplateError;

G_DECLARE_FINAL_TYPE (SysprofRecordingTemplate, sysprof_recording_template, SYSPROF, RECORDING_TEMPLATE, GObject)

GQuark                    sysprof_recording_template_error_quark          (void) G_GNUC_CONST;
SysprofProfiler          *sysprof_recording_template_apply                (SysprofRecordingTemplate  *self,
                                                                           GError                   **error);
SysprofRecordingTemplate *sysprof_recording_template_new                  (void);
SysprofRecordingTemplate *sysprof_recording_template_new_from_file        (GFile                     *file,
                                                                           GError                   **error);
gboolean                  sysprof_recording_template_save                 (SysprofRecordingTemplate  *self,
                                                                           GFile                     *file,
                                                                           GError                   **error);
SysprofDocumentLoader    *sysprof_recording_template_create_loader        (SysprofRecordingTemplate  *self,
                                                                           GFile                     *file,
                                                                           GError                   **error);
SysprofDocumentLoader    *sysprof_recording_template_create_loader_for_fd (SysprofRecordingTemplate  *self,
                                                                           int                        fd,
                                                                           GError                   **error);

G_END_DECLS
