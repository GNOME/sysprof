/* sp-proc-source.h
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

#ifndef SP_PROC_SOURCE_H
#define SP_PROC_SOURCE_H

#include "sp-source.h"

G_BEGIN_DECLS

#define SP_TYPE_PROC_SOURCE (sp_proc_source_get_type())

G_DECLARE_FINAL_TYPE (SpProcSource, sp_proc_source, SP, PROC_SOURCE, GObject)

SpSource *sp_proc_source_new              (void);
gchar    *sp_proc_source_get_command_line (GPid          pid,
                                           gboolean     *is_kernel);

G_END_DECLS

#endif /* SP_PROC_SOURCE_H */
