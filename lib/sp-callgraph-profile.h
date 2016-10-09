/* sp-callgraph-profile.h
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

#ifndef SP_CALLGRAPH_PROFILE_H
#define SP_CALLGRAPH_PROFILE_H

#include "sp-profile.h"
#include "sp-visualizer-selection.h"

G_BEGIN_DECLS

#define SP_TYPE_CALLGRAPH_PROFILE (sp_callgraph_profile_get_type())

G_DECLARE_FINAL_TYPE (SpCallgraphProfile, sp_callgraph_profile, SP, CALLGRAPH_PROFILE, GObject)

SpProfile *sp_callgraph_profile_new                (void);
SpProfile *sp_callgraph_profile_new_with_selection (SpVisualizerSelection *selection);
GQuark     sp_callgraph_profile_get_tag            (SpCallgraphProfile    *self,
                                                    const gchar           *symbol);

G_END_DECLS

#endif /* SP_CALLGRAPH_PROFILE_H */
