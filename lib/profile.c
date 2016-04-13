/* Sysprof -- Sampling, systemwide CPU profiler
 * Copyright 2004, Red Hat, Inc.
 * Copyright 2004, 2005, 2006, Soeren Sandmann
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
#include "stackstash.h"
#include "profile.h"
#include "sfile.h"

struct Profile
{
    StackStash *stash;
};

static SFormat *
create_format (void)
{
    SFormat *format;
    SForward *object_forward;
    SForward *node_forward;

    format = sformat_new();

    object_forward = sformat_declare_forward (format);
    node_forward = sformat_declare_forward (format);
    
    sformat_set_type (
	format,
	sformat_make_record (
	    format, "profile", NULL,
	    sformat_make_integer (format, "size"),
	    sformat_make_pointer (format, "call_tree", node_forward),
	    sformat_make_list (
		format, "objects", NULL,
		sformat_make_record (
		    format, "object", object_forward,
		    sformat_make_string (format, "name"),
		    sformat_make_integer (format, "total"),
		    sformat_make_integer (format, "self"),
		    NULL)),
	    sformat_make_list (
		format, "nodes", NULL,
		sformat_make_record (
		    format, "node", node_forward,
		    sformat_make_pointer (format, "object", object_forward),
		    sformat_make_pointer (format, "siblings", node_forward),
		    sformat_make_pointer (format, "children", node_forward),
		    sformat_make_pointer (format, "parent", node_forward),
		    sformat_make_integer (format, "total"),
		    sformat_make_integer (format, "self"),
		    sformat_make_integer (format, "toplevel"),
		    NULL)),
	    NULL));

    return format;
}

static void
serialize_call_tree (StackNode *node,
		     SFileOutput *output)
{
    if (!node)
	return;
    
    sfile_begin_add_record (output, "node");
    sfile_add_pointer (output, "object", U64_TO_POINTER (node->data));
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
    
    sfile_output_free (output);
    sformat_free (format);
    
    return result;
}

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

    profile->stash = stack_stash_new ((GDestroyNotify)g_free);
    
    n = sfile_begin_get_list (input, "nodes");
    for (i = 0; i < n; ++i)
    {
	StackNode *node = stack_node_new (profile->stash);
	gint32 size;
	gint32 total;
	
	sfile_begin_get_record (input, "node");
	
	sfile_get_pointer (input, "object", (gpointer *)&node->data);
	sfile_get_pointer (input, "siblings", (gpointer *)&node->siblings);
	sfile_get_pointer (input, "children", (gpointer *)&node->children);
	sfile_get_pointer (input, "parent", (gpointer *)&node->parent);
	sfile_get_integer (input, "total", &total);
	sfile_get_integer (input, "self", (gint32 *)&size);
	sfile_get_integer (input, "toplevel", NULL);

	node->total = total;
	node->size = size;
	
	sfile_end_get (input, "node", node);
	
	g_assert (node->siblings != (void *)0x11);
    }
    sfile_end_get (input, "nodes", NULL);
    sfile_end_get (input, "profile", NULL);
    
    sfile_input_free (input);
    sformat_free (format);

    stack_stash_set_root (profile->stash, root);
    
    return profile;
}

Profile *
profile_new (StackStash *stash)
{
    Profile *profile = g_new (Profile, 1);
    
    profile->stash = stack_stash_ref (stash);
    
    return profile;
}

static void
add_trace_to_tree (StackLink *trace, gint size, gpointer data)
{
    StackLink *link;
    ProfileDescendant *parent = NULL;
    ProfileDescendant **tree = data;

    link = trace;
    while (link->next)
	link = link->next;

    for (; link != NULL; link = link->prev)
    {
	gpointer address = U64_TO_POINTER (link->data);
	ProfileDescendant *prev = NULL;
	ProfileDescendant *match = NULL;

	for (match = *tree; match != NULL; prev = match, match = match->siblings)
	{
	    if (match->name == address)
	    {
		if (prev)
		{
		    /* Move to front */
		    prev->siblings = match->siblings;
		    match->siblings = *tree;
		    *tree = match;
		}
		break;
	    }
	}
	
	if (!match)
	{
	    /* Have we seen this object further up the tree? */
	    for (match = parent; match != NULL; match = match->parent)
	    {
		if (match->name == address)
		    break;
	    }
	}
	
	if (!match)
	{
	    match = g_new (ProfileDescendant, 1);
	    
	    match->name = address;
	    match->cumulative = 0;
	    match->self = 0;
	    match->children = NULL;
	    match->parent = parent;
	    match->siblings = *tree;
	    *tree = match;
	}
	
	tree = &(match->children);
	parent = match;
    }

    parent->self += size;
    while (parent)
    {
	parent->cumulative += size;
	parent = parent->parent;
    }
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
	    stack_node_foreach_trace (node, add_trace_to_tree, &tree);
	
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
profile_list_callers (Profile *profile,
		      char    *callee_name)
{
    StackNode *node;
    StackNode *callees;
    GHashTable *callers_by_name;
    GHashTable *processed_callers;
    ProfileCaller *result = NULL;

    callers_by_name = g_hash_table_new (g_direct_hash, g_direct_equal);
    processed_callers = g_hash_table_new (g_direct_hash, g_direct_equal);
    
    callees = stack_stash_find_node (profile->stash, callee_name);

    for (node = callees; node != NULL; node = node->next)
    {
	ProfileCaller *caller;
	
	if (!node->parent)
	    continue;
	
	caller = g_hash_table_lookup (
	    callers_by_name, U64_TO_POINTER (node->parent->data));
	
	if (!caller)
	{
	    caller = profile_caller_new ();
	    caller->name = U64_TO_POINTER (node->parent->data);
	    caller->total = 0;
	    caller->self = 0;
	    
	    caller->next = result;
	    result = caller;
	    
	    g_hash_table_insert (
		callers_by_name, U64_TO_POINTER (node->parent->data), caller);
	}
    }
    
    for (node = callees; node != NULL; node = node->next)
    {	
	StackNode *top_caller = node->parent;
	StackNode *top_callee = node;
	StackNode *n;
	ProfileCaller *caller;

	if (!node->parent)
	    continue;
	
	for (n = node; n && n->parent; n = n->parent)
	{
	    if (n->data == node->data		&&
		n->parent->data == node->parent->data)
	    {
		top_caller = n->parent;
		top_callee = n;
	    }
	}

	caller = g_hash_table_lookup (
	    callers_by_name, U64_TO_POINTER (node->parent->data));
	
	if (!g_hash_table_lookup (processed_callers, top_caller))
	{
	    caller->total += top_callee->total;
	    
	    g_hash_table_insert (processed_callers, top_caller, top_caller);
	}
	
	caller->self += node->size;
    }

    g_hash_table_destroy (processed_callers);
    g_hash_table_destroy (callers_by_name);

    return result;
}

#if 0
/* This code generates a list of all ancestors, rather than
 * all callers. It turned out to not work well in practice,
 * but on the other hand the single list of callers is not
 * all that great either, so we'll keep it around commented
 * out for now
 */

static void
add_to_list (gpointer key,
	     gpointer value,
	     gpointer user_data)
{
    GList **list = user_data;

    *list = g_list_prepend (*list, value);
}

static GList *
listify_hash_table (GHashTable *hash_table)
{
    GList *result = NULL;
    
    g_hash_table_foreach (hash_table, add_to_list, &result);

    return result;
}

ProfileCaller *
profile_list_ancestors (Profile       *profile,
			char          *callee_name)
{
    StackNode *callees;
    StackNode *node;
    GHashTable *callers_by_name;
    ProfileCaller *result = NULL;

    callers_by_name = g_hash_table_new (g_direct_hash, g_direct_equal);
    
    callees = stack_stash_find_node (profile->stash, callee_name);

    for (node = callees; node != NULL; node = node->next)
    {
	StackNode *n;
	gboolean seen_recursive_call;
	GHashTable *total_ancestors;
	GHashTable *all_ancestors;
	GList *all, *list;

	/* Build a list of those ancestors that should get assigned
	 * totals. If this callee does not have any recursive calls
	 * higher up, that means all of it's ancestors. If it does
	 * have a recursive call, only the one between this node
	 * and the recursive call should get assigned total
	 */
	seen_recursive_call = FALSE;
	all_ancestors = g_hash_table_new (g_direct_hash, g_direct_equal);
	total_ancestors = g_hash_table_new (g_direct_hash, g_direct_equal);
	for (n = node->parent; n; n = n->parent)
	{
	    if (!seen_recursive_call)
	    {
		g_hash_table_insert (total_ancestors, n->address, n);
	    }
	    else
	    {
		g_hash_table_remove (total_ancestors, n->address);
	    }

	    g_hash_table_insert (all_ancestors, n->address, n);

	    if (n->address == node->address)
		seen_recursive_call = TRUE;
	}

	all = listify_hash_table (all_ancestors);

	for (list = all; list; list = list->next)
	{
	    ProfileCaller *caller;
	    StackNode *ancestor = list->data;

	    caller = g_hash_table_lookup (callers_by_name, ancestor->address);

	    if (!caller)
	    {
		caller = profile_caller_new ();
		g_hash_table_insert (
		    callers_by_name, ancestor->address, caller);
		caller->name = ancestor->address;

		caller->next = result;
		result = caller;
	    }

	    caller->self += node->size;

	    if (g_hash_table_lookup (total_ancestors, ancestor->address))
		caller->total += node->total;
	}

	g_list_free (all);
	g_hash_table_destroy (all_ancestors);
	g_hash_table_destroy (total_ancestors);
    }

    return result;
}
#endif

void
profile_free (Profile *profile)
{
    stack_stash_unref (profile->stash);
    g_free (profile);
}

void
profile_descendant_free (ProfileDescendant *descendant)
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
build_object_list (StackNode *node, gpointer data)
{
    GList **objects = data;
    ProfileObject *obj;
    StackNode *n;
    
    obj = g_new (ProfileObject, 1);
    obj->name = U64_TO_POINTER (node->data);

    obj->total = compute_total (node);
    obj->self = 0;
    
    for (n = node; n != NULL; n = n->next)
	obj->self += n->size;
    
    *objects = g_list_prepend (*objects, obj);
}

GList *
profile_get_objects (Profile *profile)
{
    GList *objects = NULL;
    
    stack_stash_foreach_by_address (
	profile->stash, build_object_list, &objects);
    
    return objects;
}

gint
profile_get_size (Profile *profile)
{
    StackNode *n;
    gint size = 0;

    for (n = stack_stash_get_root (profile->stash); n != NULL; n = n->siblings)
	size += n->total;

    return size;
}
