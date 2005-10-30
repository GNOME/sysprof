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

typedef struct StackStash StackStash;

typedef struct StackNode StackNode;

struct StackNode
{
    gpointer	address;
    int		size;
    
    StackNode *	parent;
    StackNode *	siblings;
    StackNode *	children;

    StackNode * next;

    gboolean	toplevel;
};

typedef void (* StackFunction) (GSList *trace,
				gint size,
				gpointer data);

/* Stach */
StackStash *stack_stash_new       (void);
void        stack_stash_add_trace (StackStash      *stash,
				   gulong          *addrs,
				   gint	            n_addrs,
				   int              size);
void        stack_stash_foreach   (StackStash      *stash,
				   StackFunction    stack_func,
				   gpointer         data);
StackNode  *stack_stash_find_node (StackStash      *stash,
				   gpointer         address);
/* FIXME: should probably return a list */
void	    stack_node_list_leaves (StackNode  *node,
				    GList     **leaves);
typedef void (* StackNodeFunc) (StackNode *node,
				gpointer data);
void	    stack_stash_foreach_by_address (StackStash *stash,
					    StackNodeFunc func,
					    gpointer	  data);
void	    stack_stash_free	  (StackStash	   *stash);
StackNode  *stack_stash_get_root   (StackStash *stash);
StackStash *stack_stash_new_from_root (StackNode *root);

#endif
