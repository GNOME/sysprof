/* sp-multi-paned.h
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

#include "sysprof-version-macros.h"

G_BEGIN_DECLS

#define SP_TYPE_MULTI_PANED (sp_multi_paned_get_type())

SYSPROF_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (SpMultiPaned, sp_multi_paned, SP, MULTI_PANED, GtkContainer)

struct _SpMultiPanedClass
{
  GtkContainerClass parent;

  void (*resize_drag_begin) (SpMultiPaned *self,
                             GtkWidget     *child);
  void (*resize_drag_end)   (SpMultiPaned *self,
                             GtkWidget     *child);

  gpointer _reserved1;
  gpointer _reserved2;
  gpointer _reserved3;
  gpointer _reserved4;
  gpointer _reserved5;
  gpointer _reserved6;
  gpointer _reserved7;
  gpointer _reserved8;
};

SYSPROF_AVAILABLE_IN_ALL
GtkWidget *sp_multi_paned_new            (void);
SYSPROF_AVAILABLE_IN_ALL
guint      sp_multi_paned_get_n_children (SpMultiPaned *self);

G_END_DECLS
