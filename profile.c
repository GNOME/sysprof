#include <glib.h>
#include <stdlib.h>
#include <string.h>

#include "binfile.h"
#include "process.h"
#include "stackstash.h"
#include "profile.h"


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
    g_assert (v);
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

typedef struct SaveContext SaveContext;
struct SaveContext
{
    GString *str;
    GHashTable *id_by_pointer;
    GHashTable *pointer_by_id;
    int last_id; 
};

static ProfileObject *
profile_object_new (void)
{
    ProfileObject *obj = g_new (ProfileObject, 1);
    obj->total = 0;
    obj->self = 0;
    
    return obj;
}

static void
insert (SaveContext *context, int id, gpointer pointer)
{
    g_hash_table_insert (context->id_by_pointer, pointer, GINT_TO_POINTER (id));
    g_hash_table_insert (context->pointer_by_id, GINT_TO_POINTER (id), pointer);
}

static int
get_id (SaveContext *context, gpointer pointer)
{
    int id;

    if (!pointer)
	return 0;
    
    id = GPOINTER_TO_INT (g_hash_table_lookup (context->id_by_pointer, pointer));
    
    if (!id)
    {
	id = ++context->last_id;
	insert (context, id, pointer);
    }
    
    return id;
}

static void
serialize_object (gpointer key, gpointer value, gpointer data)
{
    SaveContext *context = data;
    ProfileObject *object = key;
    
    g_string_append_printf (context->str, "        <object id=\"%d\" name=\"%s\" total=\"%d\" self=\"%d\"/>\n",
			    get_id (context, object),
			    object->name,
			    object->total,
			    object->self);
}

static void
serialize_call_tree (Node *node, SaveContext *context)
{
    if (!node)
	return;
    
    g_string_append_printf (context->str,
			    "        <node id=\"%d\" object=\"%d\" siblings=\"%d\" children=\"%d\" "
			    "parent=\"%d\" next=\"%d\" total=\"%d\" self=\"%d\" toplevel=\"%d\">\n",
			    get_id (context, node),
			    get_id (context, node->object),
			    get_id (context, node->siblings),
			    get_id (context, node->children),
			    get_id (context, node->parent),
			    get_id (context, node->next),
			    node->total,
			    node->self,
			    node->toplevel);

    serialize_call_tree (node->siblings, context);
    serialize_call_tree (node->children, context);
}

gboolean
profile_save (Profile		 *profile,
	      const char	 *file_name)
{
    SaveContext context;

    context.str = g_string_new ("");
    context.id_by_pointer = g_hash_table_new (g_direct_hash, g_direct_equal);
    context.pointer_by_id = g_hash_table_new (g_direct_hash, g_direct_equal);
    context.last_id = 0;
    
    g_string_append_printf (context.str, "<profile>\n    <objects>\n");

    g_hash_table_foreach (profile->nodes_by_object, serialize_object, &context);

    g_string_append_printf (context.str, "    </objects>\n    <nodes>\n");
    
    serialize_call_tree (profile->call_tree, &context);
    
    g_string_append_printf (context.str, "    </nodes>\n</profile>\n");

    g_hash_table_destroy (context.id_by_pointer);
    g_hash_table_destroy (context.pointer_by_id);

    /* FIXME - write it to disk, not screen */
    
    g_print (context.str->str);
    
    g_string_free (context.str, TRUE);

    return TRUE;
}

typedef struct LoadContext LoadContext;
struct LoadContext
{
    GHashTable *objects_by_id;
    GHashTable *nodes_by_id;
    
};

static int
get_number (const char *input_name,
	    const char *target_name,
	    const char *value)
{
    if (strcmp (input_name, target_name) == 0)
    {
	char *end;
	int r = strtol (value, &end, 10);
	if (*end == '\0')
	    return r;
    }

    return -1;
}

#if 0
static Node *
create_node (const char **names,
	     const char **values,
	     int *id)
{
    int i;

    int object		= -1;
    int siblings	= -1;
    int children        = -1;
    int parent		= -1;
    int next		= -1;
    int total		= -1;
    int self		= -1;

    Node *node = node_new ();
    
    for (i = 0; names[i] != NULL; ++i)
    {
	*id            = get_number (names[i], "id", values[i]);
	node->object   = GINT_TO_POINTER (get_number (names[i], "object", values[i]));
	node->siblings = GINT_TO_POITNER (get_number (names[i], "siblings", values[i]));
	node->children = GINT_TO_POINTER (get_number (names[i], "children", values[i]));
	node->parent   = GINT_TO_POINTER (get_number (names[i], "parent", values[i]));
	node->next     = GINT_TO_POINTER (get_number (names[i], "next", values[i]));
	node->total    = get_number (names[i], "total", values[i]);
	node->self     = get_number (names[i], "self", values[i]);
    }
			       
    if (id == -1 || object == -1 || siblings == -1 ||
	children == -1 || parent == -1 || next == -1 ||
	total == -1 || self == -1)
    {
	return NULL;
    }

    node->object = GINT_TO_POINTER (object);
    node->siblings = GINT_TO_POINTER (siblings);
    
    
    i = 0;
	const char *name = attribute_names[i];
	const char *value = attribute_values[i];

	if (strcmp (name, "id") == 0)
	{
	}
	else if (strcmp (name, "sibling") == 0)
	{
	    
	}

	i++;
    }
    return NULL;
}

static ProfileObject *
create_object (const char **attribute_names, const char **attribute_values, int *id, GError **err)
{
    int i;

    i = 0; 
    while (attribute_names[i])
    {
	const char *name = attribute_names[i];
	const char *value = attribute_values[i];
	

	
	
	i++;
    }
    return NULL;
}
	
static void
parse_start_element (GMarkupParseContext *context,
		     const char *element_name,
		     const char **attribute_names,
		     const char **attribute_values,
		     gpointer user_data,
		     GError **err)
{
    int id;
    LoadContext *lc = user_data;
    
    if (strcmp (element_name, "object") == 0)
    {
	ProfileObject *object = create_object (attribute_names, attribute_values, &id, err);

	if (object)
	    g_hash_table_insert (lc->objects_by_id, GINT_TO_POINTER (id), object);
    }
    else if (strcmp (element_name, "node") == 0)
    {
	Node *node = create_node (attribute_names, attribute_values, &id, err);

	if (node)
	    g_hash_table_insert (lc->nodes_by_id, GINT_TO_POINTER (id), node);
    }
    else
    {
	/* ignore anything but object and node */
    }
}

static void
parse_end_element (GMarkupParseContext *context,
		   const char *element_name,
		   gpointer user_data,
		   GError **error)
{
    
}

static void
parse_error (GMarkupParseContext *context,
	     GError **error,
	     gpointer user_data)
{
}

Profile *
profile_load (const char	 *filename,
	      GError           **err)
{
    char *input = NULL;
    Profile *result = NULL;
    
    GMarkupParser parser = {
	parse_start_element, parse_end_element, NULL, NULL, NULL,
    };

    LoadContext load_context;
    GMarkupParseContext *parse_context;

    if (!g_file_get_contents (filename, &input, NULL, err))
	return NULL;

    parse_context = g_markup_parse_context_new (&parser, 0, NULL, NULL);

    if (!g_markup_parse_context_parse (parse_context, input, -1, err))
	goto out;
    
    if (!g_markup_parse_context_end_parse (parse_context, err))
	goto out;
    
    g_markup_parse_context_free (parse_context);

 out:
    g_free (input);
    
    return result;
}
#endif

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
    /* FIXME using 0 to indicate "process" is broken */
    if (address)
    {
	const Symbol *symbol = process_lookup_symbol (process, address);
	
	return g_strdup_printf ("%s", symbol->name);
    }
    else
    {
	return g_strdup_printf ("%s", process_get_cmdline (process));
    }
#if 0
    /* FIXME - don't return addresses and stuff */
    return generate_key (profile, process, address);
#endif
}

static void
ensure_profile_node (GHashTable *profile_objects, Process *process, gulong address)
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
    
    ensure_profile_node (info->profile_objects, process, 0);
    
    for (list = trace; list != NULL; list = list->next)
    {
	update ();
	ensure_profile_node (info->profile_objects, process, (gulong)list->data);
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
    Profile *profile = g_new (Profile, 1);
    Info info;
    
    profile->call_tree = NULL;
    
    profile->nodes_by_object =
	g_hash_table_new (direct_hash_no_null, g_direct_equal);
    
    profile->size = 0;
    
    info.profile = profile;
    info.profile_objects = g_hash_table_new_full (g_str_hash, g_str_equal,
						  g_free, NULL);
    
    stack_stash_foreach (stash, generate_object_table, &info);
    stack_stash_foreach (stash, generate_call_tree, &info);
    link_parents (profile->call_tree, NULL);
    
    g_hash_table_foreach (profile->nodes_by_object, compute_object_total, NULL);
    
    g_hash_table_destroy (info.profile_objects);
    
    return profile;
}

static void
add_trace_to_tree (ProfileDescendant **tree, GList *trace, guint size)
{
    GList *list;
    GList *nodes_to_unmark = NULL;
    GList *nodes_to_unmark_recursive = NULL;
    ProfileDescendant *parent = NULL;
    
    GHashTable *seen_objects =
	g_hash_table_new (direct_hash_no_null, g_direct_equal);
    
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
	    
	    seen_tree_node = g_hash_table_lookup (seen_objects, node->object);
	    
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
		
		g_hash_table_destroy (seen_objects);
		seen_objects =
		    g_hash_table_new (direct_hash_no_null, g_direct_equal);
		
		for (node = match; node != NULL; node = node->parent)
		    g_hash_table_insert (seen_objects, node->object, node);
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
	    nodes_to_unmark = g_list_prepend (nodes_to_unmark, match);
	    match->non_recursion += size;
	    ++match->marked_non_recursive;
	}
	
	if (!match->marked_total)
	{
	    nodes_to_unmark_recursive = g_list_prepend (
		nodes_to_unmark_recursive, match);
	    
	    match->total += size;
	    match->marked_total = TRUE;
	}
	
	if (!list->next)
	    match->self += size;
	
	g_hash_table_insert (seen_objects, node->object, match);
	
	tree = &(match->children);
	parent = match;
    }
    
    g_hash_table_destroy (seen_objects);
    
    for (list = nodes_to_unmark; list != NULL; list = list->next)
    {
	ProfileDescendant *tree_node = list->data;
	
	tree_node->marked_non_recursive = 0;
    }
    
    for (list = nodes_to_unmark_recursive; list != NULL; list = list->next)
    {
	ProfileDescendant *tree_node = list->data;
	
	tree_node->marked_total = FALSE;
    }
    
    g_list_free (nodes_to_unmark);
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
