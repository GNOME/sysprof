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

static guint
direct_hash_no_null (gconstpointer v)
{
    g_assert (v != NULL);
    return GPOINTER_TO_UINT (v);
}

struct Node
{
    ProfileObject *object;
    
    Node *siblings;		/* siblings in the call tree */
    Node *children;		/* children in the call tree */
    Node *parent;		/* parent in call tree */
    Node *next;			/* nodes that correspond to same object are linked though
				 * this pointer
				 */
    
    guint total;
    guint self;
    
    gboolean toplevel;
};

struct Profile
{
    gint		size;
    Node *		call_tree;
    
    /* This table is really a cache. We can build it from the call_tree */
    GHashTable *	nodes_by_object;
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

static void
add_object (gpointer key, gpointer value, gpointer data)
{
    SFileOutput *output = data;
    ProfileObject *object = key;

    sfile_begin_add_record (output, "object");

    sfile_add_string (output, "name", object->name);
    sfile_add_integer (output, "total", object->total);
    sfile_add_integer (output, "self", object->self);
    
    sfile_end_add (output, "object", object);
}

static void
serialize_call_tree (Node *node, SFileOutput *output)
{
    if (!node)
	return;
    
    sfile_begin_add_record (output, "node");
    sfile_add_pointer (output, "object", node->object);
    sfile_add_pointer (output, "siblings", node->siblings);
    sfile_add_pointer (output, "children", node->children);
    sfile_add_pointer (output, "parent", node->parent);
    sfile_add_integer (output, "total", node->total);
    sfile_add_integer (output, "self", node->self);
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
    
    SFormat *format = create_format ();
    SFileOutput *output = sfile_output_new (format);

    sfile_begin_add_record (output, "profile");

    sfile_add_integer (output, "size", profile->size);
    sfile_add_pointer (output, "call_tree", profile->call_tree);
    
    sfile_begin_add_list (output, "objects");
    g_hash_table_foreach (profile->nodes_by_object, add_object, output);
    sfile_end_add (output, "objects", NULL);

    sfile_begin_add_list (output, "nodes");
    serialize_call_tree (profile->call_tree, output);
    sfile_end_add (output, "nodes", NULL);

    sfile_end_add (output, "profile", NULL);

    result = sfile_output_save (output, file_name, err);

    sformat_free (format);
    sfile_output_free (output);

    return result;
}

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

Profile *
profile_load (const char *filename, GError **err)
{
    SFormat *format;
    SFileInput *input;
    Profile *profile;
    int n, i;
    
    format = create_format ();
    input = sfile_load (filename, format, err);

    if (!input)
	return NULL;
    
    profile = g_new (Profile, 1);

    profile->nodes_by_object =
	g_hash_table_new (direct_hash_no_null, g_direct_equal);
    
    sfile_begin_get_record (input, "profile");

    sfile_get_integer (input, "size", &profile->size);
    sfile_get_pointer (input, "call_tree", (void **)&profile->call_tree);

    n = sfile_begin_get_list (input, "objects");
    for (i = 0; i < n; ++i)
    {
	ProfileObject *obj = g_new (ProfileObject, 1);
	
	sfile_begin_get_record (input, "object");

	sfile_get_string (input, "name", &obj->name);
	sfile_get_integer (input, "total", (gint32 *)&obj->total);
	sfile_get_integer (input, "self", (gint32 *)&obj->self);
	
	sfile_end_get (input, "object", obj);
    }
    sfile_end_get (input, "objects", NULL);

    profile->call_tree = NULL;
    n = sfile_begin_get_list (input, "nodes");
    for (i = 0; i < n; ++i)
    {
	Node *node = g_new (Node, 1);

	sfile_begin_get_record (input, "node");

	sfile_get_pointer (input, "object", (gpointer *)&node->object);
	sfile_get_pointer (input, "siblings", (gpointer *)&node->siblings);
	sfile_get_pointer (input, "children", (gpointer *)&node->children);
	sfile_get_pointer (input, "parent", (gpointer *)&node->parent);
	sfile_get_integer (input, "total", (gint32 *)&node->total);
	sfile_get_integer (input, "self", (gint32 *)&node->self);
	sfile_get_integer (input, "toplevel", &node->toplevel);
	
	sfile_end_get (input, "node", node);

	g_assert (node->siblings != (void *)0x11);
    }
    sfile_end_get (input, "nodes", NULL);
    sfile_end_get (input, "profile", NULL);
    
    sformat_free (format);
    sfile_input_free (input);
    
    make_hash_table (profile->call_tree, profile->nodes_by_object);

    return profile;
}

static ProfileObject *
profile_object_new (void)
{
    ProfileObject *obj = g_new (ProfileObject, 1);
    obj->total = 0;
    obj->self = 0;
    
    return obj;
}

static void
profile_object_free (ProfileObject *obj)
{
    g_free (obj->name);
    g_free (obj);
}

static char *
generate_key (Process *process, gulong address)
{
    if (address)
    {
	const Symbol *symbol = process_lookup_symbol (process, address);
	
	return g_strdup_printf ("%p%s", (void *)symbol->address, symbol->name);
    }
    else
    {
	return g_strdup_printf ("p:%p", process_get_cmdline (process));
    }
}

static char *
generate_presentation_name (Process *process, gulong address)
{
    /* FIXME - not10
     * using 0 to indicate "process" is broken
     */
    if (address)
    {
	const Symbol *symbol = process_lookup_symbol (process, address);
	
	return g_strdup_printf ("%s", symbol->name);
    }
    else
    {
	return g_strdup_printf ("%s", process_get_cmdline (process));
    }
}

static void
ensure_profile_object (GHashTable *profile_objects, Process *process, gulong address)
{
    char *key = generate_key (process, address);
    
    if (!g_hash_table_lookup (profile_objects, key))
    {
	ProfileObject *object;
	
	object = profile_object_new ();
	object->name = generate_presentation_name (process, address);
	
	g_hash_table_insert (profile_objects, key, object);
    }
    else
    {
	g_free (key);
    }
}

static ProfileObject *
lookup_profile_object (GHashTable *profile_objects, Process *process, gulong address)
{
    ProfileObject *object;
    char *key = generate_key (process, address);
    object = g_hash_table_lookup (profile_objects, key);
    g_free (key);
    g_assert (object);
    return object;
}

typedef struct Info Info;
struct Info
{
    Profile *profile;
    GHashTable *profile_objects;
};

static void
generate_object_table (Process *process, GSList *trace, gint size, gpointer data)
{
    Info *info = data;
    GSList *list;
    
    ensure_profile_object (info->profile_objects, process, 0);
    
    for (list = trace; list != NULL; list = list->next)
    {
	update ();
	ensure_profile_object (info->profile_objects, process, (gulong)list->data);
    }
    
    info->profile->size += size;
}

static Node *
node_new ()
{
    Node *node = g_new (Node, 1);
    node->siblings = NULL;
    node->children = NULL;
    node->parent = NULL;
    node->next = NULL;
    node->object = NULL;
    node->self = 0;
    node->total = 0;
    
    return node;
}

static Node *
node_add_trace (Profile *profile, GHashTable *profile_objects, Node *node, Process *process,
		GSList *trace, gint size,
		GHashTable *seen_objects)
{
    ProfileObject *object;
    Node *match = NULL;
    
    if (!trace)
	return node;
    
    object = lookup_profile_object (profile_objects, process, (gulong)trace->data);
    for (match = node; match != NULL; match = match->siblings)
    {
	if (match->object == object)
	    break;
    }
    
    if (!match)
    {
	match = node_new ();
	match->object = object;
	match->siblings = node;
	node = match;
	
	if (g_hash_table_lookup (seen_objects, object))
	    match->toplevel = FALSE;
	else
	    match->toplevel = TRUE;
	
	match->next = g_hash_table_lookup (profile->nodes_by_object, object);
	g_hash_table_insert (profile->nodes_by_object, object, match);
    }
    
    g_hash_table_insert (seen_objects, object, GINT_TO_POINTER (1));
    
#if 0
    g_print ("%s adds %d\n", match->object->name, size);
#endif
    match->total += size;
    if (!trace->next)
	match->self += size;
    
    match->children = node_add_trace (profile, profile_objects, match->children, process, trace->next, size,
				      seen_objects);
    
    return node;
}

#if 0
static void
dump_trace (GSList *trace)
{
    g_print ("TRACE: ");
    while (trace)
    {
	g_print ("%x ", trace->data);
	trace = trace->next;
    }
    g_print ("\n\n");
}
#endif

static void
generate_call_tree (Process *process, GSList *trace, gint size, gpointer data)
{
    Info *info = data;
    Node *match = NULL;
    ProfileObject *proc = lookup_profile_object (info->profile_objects, process, 0);
    GHashTable *seen_objects;
    
    for (match = info->profile->call_tree; match; match = match->siblings)
    {
	if (match->object == proc)
	    break;
    }
    
    if (!match)
    {
	match = node_new ();
	match->object = proc;
	match->siblings = info->profile->call_tree;
	info->profile->call_tree = match;
	match->toplevel = TRUE;
    }
    
    g_hash_table_insert (info->profile->nodes_by_object, proc, match);
    
    match->total += size;
    if (!trace)
	match->self += size;
    
    seen_objects = g_hash_table_new (direct_hash_no_null, g_direct_equal);
    
    g_hash_table_insert (seen_objects, proc, GINT_TO_POINTER (1));
    
    update ();
    match->children = node_add_trace (info->profile, info->profile_objects, match->children, process,
				      trace, size, seen_objects);
    
    g_hash_table_destroy (seen_objects);
}

static void
link_parents (Node *node, Node *parent)
{
    if (!node)
	return;
    
    node->parent = parent;
    
    link_parents (node->siblings, parent);
    link_parents (node->children, node);
}

static void
compute_object_total (gpointer key, gpointer value, gpointer data)
{
    Node *node;
    ProfileObject *object = key;
    
    for (node = value; node != NULL; node = node->next)
    {
	object->self += node->self;
	if (node->toplevel)
	    object->total += node->total;
    }
}

Profile *
profile_new (StackStash *stash)
{
    Info info;

    info.profile = g_new (Profile, 1);
    info.profile->call_tree = NULL;
    info.profile->nodes_by_object =
	g_hash_table_new (direct_hash_no_null, g_direct_equal);
    info.profile->size = 0;

    /* profile objects */
    info.profile_objects = g_hash_table_new_full (g_str_hash, g_str_equal,
						  g_free, NULL);

    stack_stash_foreach (stash, generate_object_table, &info);
    stack_stash_foreach (stash, generate_call_tree, &info);
    link_parents (info.profile->call_tree, NULL);
    
    g_hash_table_foreach (info.profile->nodes_by_object, compute_object_total, NULL);
    
    g_hash_table_destroy (info.profile_objects);

    return info.profile;
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
	Node *node = list->data;
	ProfileDescendant *match = NULL;
	
	update();
	
	for (match = *tree; match != NULL; match = match->siblings)
	{
	    if (match->object == node->object)
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
		if (n->object == node->object)
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
	    
	    match->object = node->object;
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
node_list_leaves (Node *node, GList **leaves)
{
    Node *n;
    
    if (node->self > 0)
	*leaves = g_list_prepend (*leaves, node);
    
    for (n = node->children; n != NULL; n = n->siblings)
	node_list_leaves (n, leaves);
}

static void
add_leaf_to_tree (ProfileDescendant **tree, Node *leaf, Node *top)
{
    GList *trace = NULL;
    Node *node;
    
    for (node = leaf; node != top->parent; node = node->parent)
	trace = g_list_prepend (trace, node);
    
    add_trace_to_tree (tree, trace, leaf->self);
    
    g_list_free (trace);
}

ProfileDescendant *
profile_create_descendants (Profile *profile, ProfileObject *object)
{
    ProfileDescendant *tree = NULL;
    Node *node;
    
    node = g_hash_table_lookup (profile->nodes_by_object, object);
    
    while (node)
    {
	update();
	if (node->toplevel)
	{
	    GList *leaves = NULL;
	    GList *list;
	    
	    node_list_leaves (node, &leaves);
	    
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
		      ProfileObject *callee)
{
    Node *callee_node;
    Node *node;
    GHashTable *callers_by_object;
    GHashTable *seen_callers;
    ProfileCaller *result = NULL;
    
    callers_by_object =
	g_hash_table_new (g_direct_hash, g_direct_equal);
    seen_callers = g_hash_table_new (g_direct_hash, g_direct_equal);
    
    callee_node = g_hash_table_lookup (profile->nodes_by_object, callee);
    
    for (node = callee_node; node; node = node->next)
    {
	ProfileObject *object;
	if (node->parent)
	    object = node->parent->object;
	else
	    object = NULL;
	
	if (!g_hash_table_lookup (callers_by_object, object))
	{
	    ProfileCaller *caller = profile_caller_new ();
	    caller->object = object;
	    g_hash_table_insert (callers_by_object, object, caller);
	    
	    caller->next = result;
	    result = caller;
	}
    }
    
    for (node = callee_node; node != NULL; node = node->next)
    {
	Node *top_caller;
	Node *top_callee;
	Node *n;
	ProfileCaller *caller;
	ProfileObject *object;
	
	if (node->parent)
	    object = node->parent->object;
	else
	    object = NULL;
	
	caller = g_hash_table_lookup (callers_by_object, object);
	
	/* find topmost node/parent pair identical to this node/parent */
	top_caller = node->parent;
	top_callee = node;
	for (n = node; n && n->parent; n = n->parent)
	{
	    if (n->object == node->object		   &&
		n->parent->object == node->parent->object)
	    {
		top_caller = n->parent;
		top_callee = n;
	    }
	}
	
	if (!g_hash_table_lookup (seen_callers, top_caller))
	{
	    caller->total += top_callee->total;
	    
	    g_hash_table_insert (seen_callers, top_caller, (void *)0x1);
	}
	
	if (node->self > 0)
	    caller->self += node->self;
    }
    
    g_hash_table_destroy (seen_callers);
    g_hash_table_destroy (callers_by_object);
    
    return result;
    
}

static void
node_free (Node *node)
{
    if (!node)
	return;
    
    node_free (node->siblings);
    node_free (node->children);
    g_free (node);
}

static void
free_object (gpointer key, gpointer value, gpointer data)
{
    profile_object_free (key);
}

void
profile_free (Profile *profile)
{
    g_hash_table_foreach (profile->nodes_by_object, free_object, NULL);
    
    node_free (profile->call_tree);
    
    g_hash_table_destroy (profile->nodes_by_object);
    
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
build_object_list (gpointer key, gpointer value, gpointer data)
{
    ProfileObject *object = key;
    GList **objects = data;
    
    *objects = g_list_prepend (*objects, object);
}

GList *
profile_get_objects (Profile *profile)
{
    GList *objects = NULL;
    
    g_hash_table_foreach (profile->nodes_by_object, build_object_list, &objects);
    
    return objects;
}

gint
profile_get_size (Profile *profile)
{
    return profile->size;
}
