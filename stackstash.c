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

#include "stackstash.h"

typedef struct StackNode StackNode;

struct StackNode
{
    gpointer	address;
    int		size;
    
    StackNode *	parent;
    StackNode *	siblings;
    StackNode *	children;
};

struct StackStash
{
    StackNode  *root;
};

static StackNode *
stack_node_new (void)
{
    StackNode *node = g_new (StackNode, 1);
    node->siblings = NULL;
    node->children = NULL;
    node->address = NULL;
    node->parent = NULL;
    node->size = 0;
    return node;
}

static void
stack_node_destroy (gpointer p)
{
    StackNode *node = p;
    if (node)
    {
	stack_node_destroy (node->siblings);
	stack_node_destroy (node->children);
	g_free (node);
    }
}

/* Stach */
StackStash *
stack_stash_new       (void)
{
    StackStash *stash = g_new (StackStash, 1);

    stash->root = NULL;
    
    return stash;
}

void
stack_stash_add_trace (StackStash *stash,
		       gulong     *addrs,
		       int         n_addrs,
		       int         size)
{
    StackNode **location = &(stash->root);
    StackNode *parent = NULL;
    int i;

    for (i = n_addrs - 1; i >= 0; --i)
    {
	StackNode *match = NULL;
	StackNode *n;

	for (n = *location; n != NULL; n = n->siblings)
	{
	    if (n->address == (gpointer)addrs[i])
	    {
		match = n;
		break;
	    }
	}

	if (!match)
	{
	    match = stack_node_new ();
	    match->address = (gpointer)addrs[i];
	    match->siblings = *location;
	    match->parent = parent;
	    *location = match;
	}

	location = &(match->children);
	parent = match;
    }

    parent->size += size;
}

static void
do_callback (StackNode *node,
	     GSList    *first,
	     GSList    *last,
	     StackFunction stack_func,
	     gpointer data)
{
    GSList link;
    
    if (!node)
	return;

    do_callback (node->siblings, first, last, stack_func, data);

    if (!first)
	first = &link;
    if (last)
	last->next = &link;
    
    link.data = node->address;
    link.next = NULL;
    
    if (node->size)
	stack_func (first, node->size, data);

    do_callback (node->children, first, &link, stack_func, data);
}

void
stack_stash_foreach   (StackStash      *stash,
		       StackFunction    stack_func,
		       gpointer         data)
{
    do_callback (stash->root, NULL, NULL, stack_func, data);
}

static void
do_reversed_callback (StackNode *node,
		      GSList    *trace,
		      StackFunction stack_func,
		      gpointer data)
{
    GSList link;
    
    if (!node)
	return;

    link.next = trace;
    link.data = node->address;
    
    do_reversed_callback (node->siblings, trace, stack_func, data);
    do_reversed_callback (node->children, &link, stack_func, data);

    if (node->size)
	stack_func (&link, node->size, data);
}

void
stack_stash_foreach_reversed   (StackStash      *stash,
				StackFunction    stack_func,
				gpointer         data)
{
    do_reversed_callback (stash->root, NULL, stack_func, data);
}

static void
stack_node_free (StackNode *node)
{
    if (!node)
	return;

    stack_node_free (node->siblings);
    stack_node_free (node->children);

    g_free (node);
}

void
stack_stash_free	  (StackStash	   *stash)
{
    stack_node_free (stash->root);
    g_free (stash);
}
