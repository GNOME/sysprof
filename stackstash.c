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
    StackNode *	parent;
    gpointer	address;
    StackNode *	siblings;
    StackNode *	children;
    int		size;
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

static StackNode *
stack_node_add_trace (StackNode  *node,
		      GList      *bottom,
		      gint        size)
{
    StackNode *match;
    StackNode *n;

    if (!bottom)
	return node;

    for (match = node; match != NULL; match = match->siblings)
    {
	if (match->address == bottom->data)
	    break;
    }

    if (!match)
    {
	match = stack_node_new ();
	match->address = bottom->data;
	match->siblings = node;
	node = match;
    }

    match->children =
	stack_node_add_trace (match->children, bottom->next, size);

    for (n = match->children; n; n = n->siblings)
	n->parent = match;
    
    if (!bottom->next)
	match->size += size;

    return node;
}

void
stack_stash_add_trace (StackStash *stash,
		       gulong	  *addrs,
		       int         n_addrs,
		       int         size)
{
    GList *trace;
    int i;

    trace = NULL;
    for (i = 0; i < n_addrs; ++i)
	trace = g_list_prepend (trace, GINT_TO_POINTER (addrs[i]));

    stash->root = stack_node_add_trace (stash->root, trace, size);

    g_list_free (trace);
}

static void
do_callback (StackNode *node,
	     StackFunction stack_func,
	     gpointer data)
{
    if (!node)
	return;

    do_callback (node->siblings, stack_func, data);
    do_callback (node->children, stack_func, data);

    if (!node->children)
    {
	StackNode *n;
	GSList *trace = NULL;

	for (n = node; n != NULL; n = n->parent)
	    trace = g_slist_prepend (trace, n->address);

	stack_func (trace, node->size, data);

	g_slist_free (trace);
    }
}

void
stack_stash_foreach   (StackStash      *stash,
		       StackFunction    stack_func,
		       gpointer         data)
{
    do_callback (stash->root, stack_func, data);
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
