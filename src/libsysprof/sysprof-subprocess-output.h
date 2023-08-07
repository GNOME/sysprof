/* sysprof-subprocess-output.h
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

#include "sysprof-instrument.h"
#include "sysprof-recording.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_SUBPROCESS_OUTPUT         (sysprof_subprocess_output_get_type())
#define SYSPROF_IS_SUBPROCESS_OUTPUT(obj)      G_TYPE_CHECK_INSTANCE_TYPE(obj, SYSPROF_TYPE_SUBPROCESS_OUTPUT)
#define SYSPROF_SUBPROCESS_OUTPUT(obj)         G_TYPE_CHECK_INSTANCE_CAST(obj, SYSPROF_TYPE_SUBPROCESS_OUTPUT, SysprofSubprocessOutput)
#define SYSPROF_SUBPROCESS_OUTPUT_CLASS(klass) G_TYPE_CHECK_CLASS_CAST(klass, SYSPROF_TYPE_SUBPROCESS_OUTPUT, SysprofSubprocessOutputClass)

typedef struct _SysprofSubprocessOutput      SysprofSubprocessOutput;
typedef struct _SysprofSubprocessOutputClass SysprofSubprocessOutputClass;

SYSPROF_AVAILABLE_IN_ALL
GType                  sysprof_subprocess_output_get_type            (void) G_GNUC_CONST;
SYSPROF_AVAILABLE_IN_ALL
SysprofRecordingPhase  sysprof_subprocess_output_get_phase           (SysprofSubprocessOutput *self);
SYSPROF_AVAILABLE_IN_ALL
void                   sysprof_subprocess_output_set_phase           (SysprofSubprocessOutput *self,
                                                                      SysprofRecordingPhase    phase);
SYSPROF_AVAILABLE_IN_ALL
SysprofInstrument     *sysprof_subprocess_output_new                 (void);
SYSPROF_AVAILABLE_IN_ALL
const char            *sysprof_subprocess_output_get_stdout_path     (SysprofSubprocessOutput *self);
SYSPROF_AVAILABLE_IN_ALL
const char            *sysprof_subprocess_output_get_command_cwd     (SysprofSubprocessOutput *self);
SYSPROF_AVAILABLE_IN_ALL
const char * const    *sysprof_subprocess_output_get_command_argv    (SysprofSubprocessOutput *self);
SYSPROF_AVAILABLE_IN_ALL
const char * const    *sysprof_subprocess_output_get_command_environ (SysprofSubprocessOutput *self);
SYSPROF_AVAILABLE_IN_ALL
void                   sysprof_subprocess_output_set_stdout_path     (SysprofSubprocessOutput *self,
                                                                      const char              *stdout_path);
SYSPROF_AVAILABLE_IN_ALL
void                   sysprof_subprocess_output_set_command_cwd     (SysprofSubprocessOutput *self,
                                                                      const char              *command_cwd);
SYSPROF_AVAILABLE_IN_ALL
void                   sysprof_subprocess_output_set_command_argv    (SysprofSubprocessOutput *self,
                                                                      const char * const      *command_argv);
SYSPROF_AVAILABLE_IN_ALL
void                   sysprof_subprocess_output_set_command_environ (SysprofSubprocessOutput *self,
                                                                      const char * const      *command_environ);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofSubprocessOutput, g_object_unref)

G_END_DECLS
