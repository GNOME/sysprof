/* sysprof-stack-node-item.h
 *
 * Copyright 2023 Corentin Noël <corentin.noel@collabora.com>
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

#include <glib-object.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_STACK_NODE_ITEM (sysprof_stack_node_item_get_type())

G_DECLARE_FINAL_TYPE (SysprofStackNodeItem, sysprof_stack_node_item, SYSPROF, STACK_NODE_ITEM, GObject)

SysprofStackNodeItem *sysprof_stack_node_item_new      (gpointer node, gdouble profile_size);
gpointer              sysprof_stack_node_item_get_node (SysprofStackNodeItem *self);

G_END_DECLS
