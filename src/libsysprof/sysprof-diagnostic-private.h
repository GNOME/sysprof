/* sysprof-diagnostic-private.h
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

#include "sysprof-diagnostic.h"
#include "sysprof-document-frame.h"

G_BEGIN_DECLS

struct _SysprofDiagnostic
{
  GObject parent_instance;
};

struct _SysprofDiagnosticClass
{
  GObjectClass parent_class;
};

SysprofDiagnostic *_sysprof_diagnostic_new         (char                  *domain,
                                                    char                  *message,
                                                    gboolean               fatal);
void               _sysprof_diagnostic_set_details (SysprofDiagnostic     *self,
                                                    const char            *domain,
                                                    const char            *category,
                                                    const char            *message,
                                                    const SysprofTimeSpan *time_span);
void               _sysprof_diagnostic_add_frame   (SysprofDiagnostic     *self,
                                                    SysprofDocumentFrame  *frame);

G_END_DECLS
