/* Sysprof -- Sampling, systemwide CPU profiler
 * Copyright 2004, Red Hat, Inc.
 * Copyright 2004, 2005, Soeren Sandmann
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef STACK_STASH_H
#define STACK_STASH_H

#include <glib.h>
#include <stdint.h>

typedef struct StackStash StackStash;
typedef struct StackNode StackNode;
typedef struct StackLink StackLink;

#define U64_TO_POINTER(u)	((void *)(intptr_t)u)
#define POINTER_TO_U64(p)	((uint64_t)(intptr_t)p)

struct StackNode
{
    uint64_t	data;

    guint	total : 32;
    guint	size : 31;
    guint	toplevel : 1;
    
    StackNode *	parent;
    StackNode *	siblings;
    StackNode *	children;

    StackNode * next;
};

struct StackLink
{
    uint64_t	data;
    StackLink  *next;
    StackLink  *prev;
};

typedef void (* StackFunction) (StackLink *trace,
				gint       size,
				gpointer   data);

typedef void (* StackNodeFunc) (StackNode *node,
				gpointer   data);

/* Stach */
StackStash *stack_stash_new                (GDestroyNotify  destroy);
StackNode * stack_node_new                 (StackStash     *stash);
StackNode * stack_stash_add_trace          (StackStash     *stash,
					    uint64_t       *addrs,
					    gint            n_addrs,
					    int             size);
void        stack_stash_foreach            (StackStash     *stash,
					    StackFunction   stack_func,
					    gpointer        data);
void        stack_node_foreach_trace       (StackNode      *node,
					    StackFunction   stack_func,
					    gpointer        data);
StackNode  *stack_stash_find_node          (StackStash     *stash,
					    gpointer        address);
void        stack_stash_foreach_by_address (StackStash     *stash,
					    StackNodeFunc   func,
					    gpointer        data);
StackNode  *stack_stash_get_root           (StackStash     *stash);
StackStash *stack_stash_ref                (StackStash     *stash);
void        stack_stash_unref              (StackStash     *stash);
void	    stack_stash_set_root	   (StackStash     *stash,
					    StackNode      *root);

#endif
