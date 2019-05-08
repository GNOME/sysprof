/* sp-callgraph-view.h
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
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

#ifndef SP_CALLGRAPH_VIEW_H
#define SP_CALLGRAPH_VIEW_H

#include <gtk/gtk.h>

#include "sp-callgraph-profile.h"

G_BEGIN_DECLS

#define SP_TYPE_CALLGRAPH_VIEW (sp_callgraph_view_get_type())

G_DECLARE_DERIVABLE_TYPE (SpCallgraphView, sp_callgraph_view, SP, CALLGRAPH_VIEW, GtkBin)

struct _SpCallgraphViewClass
{
  GtkBinClass parent_class;

  void (*go_previous) (SpCallgraphView *self);

  gpointer padding[8];
};

GtkWidget          *sp_callgraph_view_new             (void);
SpCallgraphProfile *sp_callgraph_view_get_profile     (SpCallgraphView    *self);
void                sp_callgraph_view_set_profile     (SpCallgraphView    *self,
                                                       SpCallgraphProfile *profile);
gchar              *sp_callgraph_view_screenshot      (SpCallgraphView    *self);
guint               sp_callgraph_view_get_n_functions (SpCallgraphView    *self);

G_END_DECLS

#endif /* SP_CALLGRAPH_VIEW_H */
