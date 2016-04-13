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

struct StackStash
{
    int			ref_count;
    StackNode *		root;
    GHashTable *	nodes_by_data;
    GDestroyNotify	destroy;

    StackNode *		cached_nodes;
    GPtrArray *		blocks;
};

static void
decorate_node (StackNode *node,
	       StackStash *stash)
{
    StackNode *n;

    if (!node)
	return;

    decorate_node (node->siblings, stash);
    decorate_node (node->children, stash);

    node->next = g_hash_table_lookup (stash->nodes_by_data, &node->data);
    g_hash_table_insert (stash->nodes_by_data, &node->data, node);
	
    /* FIXME: This could be done more efficiently
     * by keeping track of the ancestors we have seen.
     */
    node->toplevel = TRUE;
    for (n = node->parent; n != NULL; n = n->parent)
    {
	if (n->data == node->data)
	{
	    node->toplevel = FALSE;
	    break;
	}
    }
}

static unsigned int
address_hash (gconstpointer key)
{
    const uint64_t *addr = key;

    return *addr;
}

static gboolean
address_equal (gconstpointer key1, gconstpointer key2)
{
    const uint64_t *addr1 = key1;
    const uint64_t *addr2 = key2;

    return *addr1 == *addr2;
}

static void
stack_stash_decorate (StackStash *stash)
{
    if (stash->nodes_by_data)
	return;

    stash->nodes_by_data = g_hash_table_new (address_hash, address_equal);

    decorate_node (stash->root, stash);
}

static void
free_key (gpointer key,
	  gpointer value,
	  gpointer data)
{
    GDestroyNotify destroy = data;
    uint64_t u64 = *(uint64_t *)key;

    destroy (U64_TO_POINTER (u64));
}

static void
stack_stash_undecorate (StackStash *stash)
{
    if (stash->nodes_by_data)
    {
	if (stash->destroy)
	{
	    g_hash_table_foreach (
		stash->nodes_by_data, free_key, stash->destroy);
	}
	
	g_hash_table_destroy (stash->nodes_by_data);
	stash->nodes_by_data = NULL;
    }
}

static GHashTable *
get_nodes_by_data (StackStash *stash)
{
    if (!stash->nodes_by_data)
	stack_stash_decorate (stash);

    return stash->nodes_by_data;
}

StackNode *
stack_node_new (StackStash *stash)
{
    StackNode *node;

    if (!stash->cached_nodes)
    {
#define BLOCK_SIZE 32768
#define N_NODES (BLOCK_SIZE / sizeof (StackNode))

	StackNode *block = g_malloc (BLOCK_SIZE);
	int i;

	for (i = 0; i < N_NODES; ++i)
	{
	    block[i].next = stash->cached_nodes;
	    stash->cached_nodes = &(block[i]);
	}

	g_ptr_array_add (stash->blocks, block);
    }

    node = stash->cached_nodes;
    stash->cached_nodes = node->next;

    node->siblings = NULL;
    node->children = NULL;
    node->data = 0;
    node->parent = NULL;
    node->size = 0;
    node->next = NULL;
    node->total = 0;
    
    return node;
}

/* "destroy", if non-NULL, is called once on every address */
static StackStash *
create_stack_stash (GDestroyNotify destroy)
{
    StackStash *stash = g_new (StackStash, 1);

    stash->root = NULL;
    stash->nodes_by_data = NULL;
    stash->ref_count = 1;
    stash->destroy = destroy;
    
    stash->cached_nodes = NULL;
    stash->blocks = g_ptr_array_new ();

    return stash;
}

/* Stach */
StackStash *
stack_stash_new (GDestroyNotify destroy)
{
    return create_stack_stash (destroy);
}


static void
stack_stash_free (StackStash *stash)
{
    int i;
    
    stack_stash_undecorate (stash);

    for (i = 0; i < stash->blocks->len; ++i)
	g_free (stash->blocks->pdata[i]);
    
    g_ptr_array_free (stash->blocks, TRUE);
    
    g_free (stash);
}

StackNode *
stack_stash_add_trace (StackStash *stash,
		       uint64_t   *addrs,
		       int         n_addrs,
		       int         size)
{
    StackNode **location = &(stash->root);
    StackNode *parent = NULL;
    int i;

    if (!n_addrs)
	return NULL;

    if (stash->nodes_by_data)
	stack_stash_undecorate (stash);
    
    for (i = n_addrs - 1; i >= 0; --i)
    {
	StackNode *match = NULL;
	StackNode *prev;

	/* FIXME: On x86-64 we don't get proper stacktraces which means
	 * each node can have tons of children. That makes this loop
	 * here show up on profiles.
	 *
	 * Not sure what can be done about it aside from actually fixing
	 * x86-64 to get stacktraces.
	 */
	prev = NULL;
	for (match = *location; match; prev = match, match = match->siblings)
	{
	    if (match->data == addrs[i])
	    {
		if (prev)
		{
		    /* move to front */
		    prev->siblings = match->siblings;
		    match->siblings = *location;
		    *location = match;
		}
		
		break;
	    }
	}

	if (!match)
	{
	    match = stack_node_new (stash);
	    match->data = addrs[i];
	    match->siblings = *location;
	    match->parent = parent;
	    *location = match;
	}

	match->total += size;

	location = &(match->children);
	parent = match;
    }

    parent->size += size;

    return parent;
}

static void
do_callback (StackNode *node,
	     StackLink *trace,
	     StackFunction func,
	     gpointer data)
{
    StackLink link;

    if (trace)
	trace->prev = &link;
	
    link.next = trace;
    link.prev = NULL;
    
    while (node)
    {
	link.data = node->data;
	
	if (node->size)
	    func (&link, node->size, data);
	
	do_callback (node->children, &link, func, data);
	
	node = node->siblings;
    }

    if (trace)
	trace->prev = NULL;
}

void
stack_stash_foreach (StackStash      *stash,
		     StackFunction    stack_func,
		     gpointer         data)
{
    do_callback (stash->root, NULL, stack_func, data);
}

void
stack_node_foreach_trace (StackNode     *node,
			  StackFunction  func,
			  gpointer       data)
{
    StackLink link;

    link.next = NULL;
    link.data = node->data;
    link.prev = NULL;

    if (node->size)
	func (&link, node->size, data);
    
    do_callback (node->children, &link, func, data);
}

void
stack_stash_unref (StackStash *stash)
{
    stash->ref_count--;
    if (stash->ref_count == 0)
	stack_stash_free (stash);
}

StackStash *
stack_stash_ref (StackStash *stash)
{
    stash->ref_count++;
    return stash;
}

StackNode *
stack_stash_find_node (StackStash      *stash,
		       gpointer         data)
{
    uint64_t u64 = POINTER_TO_U64 (data);
    
    g_return_val_if_fail (stash != NULL, NULL);
    
    return g_hash_table_lookup (get_nodes_by_data (stash), &u64);
}

typedef struct
{
    StackNodeFunc func;
    gpointer	  data;
} Info;

static void
do_foreach (gpointer key, gpointer value, gpointer data)
{
    Info *info = data;

    info->func (value, info->data);
}

void
stack_stash_foreach_by_address (StackStash *stash,
				StackNodeFunc func,
				gpointer      data)
{
    Info info;
    info.func = func;
    info.data = data;
	
    g_hash_table_foreach (get_nodes_by_data (stash), do_foreach, &info);
}

StackNode  *
stack_stash_get_root (StackStash *stash)
{
    return stash->root;
}

void
stack_stash_set_root (StackStash     *stash,
		      StackNode      *root)
{
    g_return_if_fail (stash->root == NULL);
    
    stash->root = root;
}
