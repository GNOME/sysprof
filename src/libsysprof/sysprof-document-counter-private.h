/* sysprof-document-counter-private.h
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

#include "sysprof-document-counter.h"

G_BEGIN_DECLS

struct _SysprofDocumentCounter
{
  GObject parent_instance;
  GRefString *category;
  GRefString *description;
  GRefString *name;
  GArray *values;
  double min_value;
  double max_value;
  gint64 begin_time;
  guint id;
  guint type;
};

SysprofDocumentCounter *_sysprof_document_counter_new             (guint                   id,
                                                                   guint                   type,
                                                                   GRefString             *category,
                                                                   GRefString             *name,
                                                                   GRefString             *description,
                                                                   GArray                 *values,
                                                                   gint64                  begin_time);
void                    _sysprof_document_counter_calculate_range (SysprofDocumentCounter *self);

G_END_DECLS
