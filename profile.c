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

#include <glib.h>
#include <stdlib.h>
#include <string.h>

#include "binfile.h"
#include "process.h"
#include "stackstash.h"
#include "profile.h"
#include "sfile.h"

typedef struct Node Node;

static void
update()
{
#if 0
    while (g_main_pending())
	g_main_iteration (FALSE);
#endif
}

struct Profile
{
    StackStash *	stash;
};

static SFormat *
create_format (void)
{
    SType object_type = 0;
    SType node_type = 0;
    
    return sformat_new (
	sformat_new_record (
	    "profile", NULL,
	    sformat_new_integer ("size"),
	    sformat_new_pointer ("call_tree", &node_type),
	    sformat_new_list (
		"objects", NULL,
		sformat_new_record (
		    "object", &object_type,
		    sformat_new_string ("name"),
		    sformat_new_integer ("total"),
		    sformat_new_integer ("self"),
		    NULL)),
	    sformat_new_list (
		"nodes", NULL,
		sformat_new_record (
		    "node", &node_type,
		    sformat_new_pointer ("object", &object_type),
		    sformat_new_pointer ("siblings", &node_type),
		    sformat_new_pointer ("children", &node_type),
		    sformat_new_pointer ("parent", &node_type),
		    sformat_new_integer ("total"),
		    sformat_new_integer ("self"),
		    sformat_new_integer ("toplevel"),
		    NULL)),
	    NULL));
}

static int
compute_total (StackNode *node)
{
    StackNode *n;
    int total = 0;

    for (n = node; n != NULL; n = n->next)
    {
	if (n->toplevel)
	    total += n->total;
    }
    
    return total;
}

static void
serialize_call_tree (StackNode *node,
		     SFileOutput *output)
{
    if (!node)
	return;
    
    sfile_begin_add_record (output, "node");
    sfile_add_pointer (output, "object", node->address);
    sfile_add_pointer (output, "siblings", node->siblings);
    sfile_add_pointer (output, "children", node->children);
    sfile_add_pointer (output, "parent", node->parent);
    sfile_add_integer (output, "total", node->total);
    sfile_add_integer (output, "self", node->size);
    sfile_add_integer (output, "toplevel", node->toplevel);
    sfile_end_add (output, "node", node);
    
    serialize_call_tree (node->siblings, output);
    serialize_call_tree (node->children, output);
}

gboolean
profile_save (Profile		 *profile,
	      const char	 *file_name,
	      GError            **err)
{
    gboolean result;
    
    GList *profile_objects;
    GList *list;
    
    SFormat *format = create_format ();
    SFileOutput *output = sfile_output_new (format);
    
    sfile_begin_add_record (output, "profile");
    
    sfile_add_integer (output, "size", profile_get_size (profile));
    sfile_add_pointer (output, "call_tree",
		       stack_stash_get_root (profile->stash));
    
    profile_objects = profile_get_objects (profile);
    sfile_begin_add_list (output, "objects");
    for (list = profile_objects; list != NULL; list = list->next)
    {
	ProfileObject *object = list->data;
	
	sfile_begin_add_record (output, "object");
	
	sfile_add_string (output, "name", object->name);
	sfile_add_integer (output, "total", object->total);
	sfile_add_integer (output, "self", object->self);
	
	sfile_end_add (output, "object", object->name);
    }
    g_list_foreach (profile_objects, (GFunc)g_free, NULL);
    g_list_free (profile_objects);
    
    sfile_end_add (output, "objects", NULL);
    
    sfile_begin_add_list (output, "nodes");
    serialize_call_tree (stack_stash_get_root (profile->stash), output);
    sfile_end_add (output, "nodes", NULL);
    
    sfile_end_add (output, "profile", NULL);
    
    result = sfile_output_save (output, file_name, err);
    
    sformat_free (format);
    sfile_output_free (output);
    
    return result;
}

#if 0
static void
make_hash_table (Node *node, GHashTable *table)
{
    if (!node)
	return;
    
    g_assert (node->object);
    
    node->next = g_hash_table_lookup (table, node->object);
    g_hash_table_insert (table, node->object, node);
    
    g_assert (node->siblings != (void *)0x11);
    
    make_hash_table (node->siblings, table);
    make_hash_table (node->children, table);
}
#endif

Profile *
profile_load (const char *filename, GError **err)
{
    SFormat *format;
    SFileInput *input;
    Profile *profile;
    int n, i;
    StackNode *root;
    
    format = create_format ();
    input = sfile_load (filename, format, err);
    
    if (!input)
	return NULL;
    
    profile = g_new (Profile, 1);
    
    sfile_begin_get_record (input, "profile");
    
    sfile_get_integer (input, "size", NULL);
    sfile_get_pointer (input, "call_tree", (gpointer *)&root);
    
    n = sfile_begin_get_list (input, "objects");
    for (i = 0; i < n; ++i)
    {
	char *string;
	
	sfile_begin_get_record (input, "object");
	
	sfile_get_string (input, "name", &string);
	sfile_get_integer (input, "total", NULL);
	sfile_get_integer (input, "self", NULL);
	
	sfile_end_get (input, "object", string);
    }
    sfile_end_get (input, "objects", NULL);
    
    n = sfile_begin_get_list (input, "nodes");
    for (i = 0; i < n; ++i)
    {
	StackNode *node = g_new (StackNode, 1);
	
	sfile_begin_get_record (input, "node");
	
	sfile_get_pointer (input, "object", (gpointer *)&node->address);
	sfile_get_pointer (input, "siblings", (gpointer *)&node->siblings);
	sfile_get_pointer (input, "children", (gpointer *)&node->children);
	sfile_get_pointer (input, "parent", (gpointer *)&node->parent);
	sfile_get_integer (input, "total", &node->total);
	sfile_get_integer (input, "self", (gint32 *)&node->size);
	sfile_get_integer (input, "toplevel", &node->toplevel);
	
	sfile_end_get (input, "node", node);
	
	g_assert (node->siblings != (void *)0x11);
    }
    sfile_end_get (input, "nodes", NULL);
    sfile_end_get (input, "profile", NULL);
    
    sformat_free (format);
    sfile_input_free (input);
    
    profile->stash = stack_stash_new_from_root (root);
    
    return profile;
}

Profile *
profile_new (StackStash *stash)
{
    Profile *profile = g_new (Profile, 1);
    
    profile->stash = stash;
    
    return profile;
}

static void
add_trace_to_tree (ProfileDescendant **tree, GList *trace, guint size)
{
    GList *list;
    GPtrArray *nodes_to_unmark = g_ptr_array_new ();
    GPtrArray *nodes_to_unmark_recursive = g_ptr_array_new ();
    ProfileDescendant *parent = NULL;
    int i, len;
    
    GPtrArray *seen_objects = g_ptr_array_new ();
    
    for (list = trace; list != NULL; list = list->next)
    {
	StackNode *node = list->data;
	ProfileDescendant *match = NULL;
	
	update();
	
	for (match = *tree; match != NULL; match = match->siblings)
	{
	    if (match->name == node->address)
		break;
	}
	
	if (!match)
	{
	    ProfileDescendant *seen_tree_node;
	    
	    /* Have we seen this object further up the tree? */
	    seen_tree_node = NULL;
	    for (i = 0; i < seen_objects->len; ++i)
	    {
		ProfileDescendant *n = seen_objects->pdata[i];
		if (n->name == node->address)
		    seen_tree_node = n;
	    }
	    
	    if (seen_tree_node)
	    {
		ProfileDescendant *node;
		
		g_assert (parent);
		
		for (node = parent; node != seen_tree_node->parent; node = node->parent)
		{
		    node->non_recursion -= size;
		    --node->marked_non_recursive;
		}
		
		match = seen_tree_node;
		
		g_ptr_array_remove_range (seen_objects, 0, seen_objects->len);
		for (node = match; node != NULL; node = node->parent)
		    g_ptr_array_add (seen_objects, node);
	    }
	}
	
	if (!match)
	{
	    match = g_new (ProfileDescendant, 1);
	    
	    match->name = node->address;
	    match->non_recursion = 0;
	    match->total = 0;
	    match->self = 0;
	    match->children = NULL;
	    match->marked_non_recursive = 0;
	    match->marked_total = FALSE;
	    match->parent = parent;
	    
	    match->siblings = *tree;
	    *tree = match;
	}
	
	if (!match->marked_non_recursive)
	{
	    g_ptr_array_add (nodes_to_unmark, match);
	    match->non_recursion += size;
	    ++match->marked_non_recursive;
	}
	
	if (!match->marked_total)
	{
	    g_ptr_array_add (nodes_to_unmark_recursive, match);
	    
	    match->total += size;
	    match->marked_total = TRUE;
	}
	
	if (!list->next)
	    match->self += size;
	
	g_ptr_array_add (seen_objects, match);
	
	tree = &(match->children);
	parent = match;
    }
    
    g_ptr_array_free (seen_objects, TRUE);
    
    len = nodes_to_unmark->len;
    for (i = 0; i < len; ++i)
    {
	ProfileDescendant *tree_node = nodes_to_unmark->pdata[i];
	
	tree_node->marked_non_recursive = 0;
    }
    
    len = nodes_to_unmark_recursive->len;
    for (i = 0; i < len; ++i)
    {
	ProfileDescendant *tree_node = nodes_to_unmark_recursive->pdata[i];
	
	tree_node->marked_total = FALSE;
    }
    
    g_ptr_array_free (nodes_to_unmark, TRUE);
    g_ptr_array_free (nodes_to_unmark_recursive, TRUE);
}

static void
add_leaf_to_tree (ProfileDescendant **tree, StackNode *leaf, StackNode *top)
{
    GList *trace = NULL;
    StackNode *node;
    
    for (node = leaf; node != top->parent; node = node->parent)
	trace = g_list_prepend (trace, node);
    
    add_trace_to_tree (tree, trace, leaf->size);
    
    g_list_free (trace);
}

ProfileDescendant *
profile_create_descendants (Profile *profile,
			    char *object_name)
{
    ProfileDescendant *tree = NULL;
    
    StackNode *node = stack_stash_find_node (profile->stash, object_name);
    
    while (node)
    {
	if (node->toplevel)
	{
	    GList *leaves = NULL;
	    GList *list;
	    
	    stack_node_list_leaves (node, &leaves);
	    
	    for (list = leaves; list != NULL; list = list->next)
		add_leaf_to_tree (&tree, list->data, node);
	    
	    g_list_free (leaves);
	}
	
	node = node->next;
    }
    
    return tree;
}

static ProfileCaller *
profile_caller_new (void)
{
    ProfileCaller *caller = g_new (ProfileCaller, 1);
    caller->next = NULL;
    caller->self = 0;
    caller->total = 0;
    return caller;
}

ProfileCaller *
profile_list_callers (Profile       *profile,
		      char          *callee_name)
{
    StackNode *callee_node;
    StackNode *node;
    GHashTable *callers_by_object;
    GHashTable *seen_callers;
    ProfileCaller *result = NULL;
    
    callers_by_object =
	g_hash_table_new (g_direct_hash, g_direct_equal);
    seen_callers = g_hash_table_new (g_direct_hash, g_direct_equal);
    
    callee_node = stack_stash_find_node (profile->stash, callee_name);
    
    for (node = callee_node; node; node = node->next)
    {
	char *object;
	
	if (node->parent)
	    object = node->parent->address;
	else
	    object = NULL;
	
	if (!g_hash_table_lookup (callers_by_object, object))
	{
	    ProfileCaller *caller = profile_caller_new ();
	    caller->name = object;
	    g_hash_table_insert (callers_by_object, object, caller);
	    
	    caller->next = result;
	    result = caller;
	}
    }
    
    for (node = callee_node; node != NULL; node = node->next)
    {
	StackNode *top_caller;
	StackNode *top_callee;
	StackNode *n;
	ProfileCaller *caller;
	char *object;
	
	if (node->parent)
	    object = node->parent->address;
	else
	    object = NULL;
	
	caller = g_hash_table_lookup (callers_by_object, object);
	
	/* find topmost node/parent pair identical to this node/parent */
	top_caller = node->parent;
	top_callee = node;
	for (n = node; n && n->parent; n = n->parent)
	{
	    if (n->address == node->address		   &&
		n->parent->address == node->parent->address)
	    {
		top_caller = n->parent;
		top_callee = n;
	    }
	}
	
	if (!g_hash_table_lookup (seen_callers, top_caller))
	{
	    caller->total += compute_total (top_callee);
	    
	    g_hash_table_insert (seen_callers, top_caller, (void *)0x1);
	}
	
	if (node->size > 0)
	    caller->self += node->size;
    }
    
    g_hash_table_destroy (seen_callers);
    g_hash_table_destroy (callers_by_object);
    
    return result;
}

void
profile_free (Profile *profile)
{
    /* FIXME unref stash */
    g_free (profile);
}

void
profile_descendant_free    (ProfileDescendant *descendant)
{
    if (!descendant)
	return;
    
    profile_descendant_free (descendant->siblings);
    profile_descendant_free (descendant->children);
    
    g_free (descendant);
}

void
profile_caller_free (ProfileCaller *caller)
{
    if (!caller)
	return;
    
    profile_caller_free (caller->next);
    g_free (caller);
}

static void
build_object_list (StackNode *node, gpointer data)
{
    GList **objects = data;
    ProfileObject *obj;
    StackNode *n;
    
    obj = g_new (ProfileObject, 1);
    obj->name = node->address;
    
    obj->total = compute_total (node);

    obj->self = 0;
    for (n = node; n != NULL; n = n->siblings)
	obj->self += n->size;
    
    *objects = g_list_prepend (*objects, obj);
}

GList *
profile_get_objects (Profile *profile)
{
    GList *objects = NULL;
    
    stack_stash_foreach_by_address (profile->stash, build_object_list, &objects);
    
    /* FIXME: everybody still assumes that they don't have to free the
     * objects in the list, but these days they do, and so we are leaking.
     */
    
    return objects;
}

gint
profile_get_size (Profile *profile)
{
    StackNode *n;
    gint size = 0;
    
    for (n = stack_stash_get_root (profile->stash); n != NULL; n = n->siblings)
	size += compute_total (n);

    return size;
}
