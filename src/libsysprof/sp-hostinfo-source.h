/* sp-hostinfo-source.h
 *
 * Copyright 2016 Christian Hergert <christian@hergert.me>
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

#ifndef SP_HOSTINFO_SOURCE_H
#define SP_HOSTINFO_SOURCE_H

#include "sp-source.h"

G_BEGIN_DECLS

#define SP_TYPE_HOSTINFO_SOURCE (sp_hostinfo_source_get_type())

G_DECLARE_FINAL_TYPE (SpHostinfoSource, sp_hostinfo_source, SP, HOSTINFO_SOURCE, GObject)

SpSource *sp_hostinfo_source_new (void);

G_END_DECLS

#endif /* SP_HOSTINFO_SOURCE_H */

