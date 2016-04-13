/* sp-callgraph-profile-private.h
 *
 * Copyright (C) 2016 Christian Hergert <christian@hergert.me>
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

#ifndef SP_CALLGRAPH_PROFILE_PRIVATE_H
#define SP_CALLGRAPH_PROFILE_PRIVATE_H

#include "sp-callgraph-profile.h"
#include "stackstash.h"

G_BEGIN_DECLS

StackStash *sp_callgraph_profile_get_stash (SpCallgraphProfile *self);

G_END_DECLS

#endif /* SP_CALLGRAPH_PROFILE_PRIVATE_H */
