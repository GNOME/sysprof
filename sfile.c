/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*- */

/*  Lac - Library for asynchronous communication
 *  Copyright (C) 2004  Søren Sandmann (sandmann@daimi.au.dk)
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the 
 *  Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 *  Boston, MA  02111-1307, USA.
 */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include "sfile.h"

typedef struct ReadItem ReadItem;

typedef enum
{
    BEGIN_RECORD,
    BEGIN_LIST,
    END,
    READ_POINTER,
    READ_INTEGER,
    READ_STRING
} ReadAction;

typedef enum
{
    RECORD,
    LIST,
    POINTER,
    INTEGER,
    STRING
} SFormatType;

struct SFormat
{
    SFormatType type;

    char *name;
    
    union
    {
	struct
	{
	    GQueue *fields;
	} record;
	
	struct
	{
	    SFormat *content;
	} list;
    } u;
};

struct ReadItem
{
    ReadAction action;

    union
    {
        struct
        {
            int         id;
            gpointer    pointer;        /* Points to the user created object */
        } end;
        
        struct
        {
            int         id;
        } read_pointer;

        struct
        {
            int         n_items;
        } read_list;
        
        struct
        {
            int         value;
        } read_integer;
        
        struct
        {
            char *      value;
        } read_string;
    } u;
};

struct SFile
{
    int n_items;
    ReadItem *items;
    ReadItem *current_item;
    GHashTable *locations_by_id;
    GHashTable *objects_by_id;
};

/* defining types */

static SFormat *
create_sformat (const char *name,
                SFormatType type)
{
    SFormat *sformat = g_new0 (SFormat, 1);

    sformat->name = g_strdup (name);
    sformat->type = type;

    return sformat;
}

SFormat *
sformat_new_record  (const char *     name,
                     SFormat      *content1,
                     ...)
{
    va_list args;
    SFormat *sformat = create_sformat (name, RECORD);
    SFormat *field;
    GQueue *fields = g_queue_new ();
    
    va_start (args, content1);
    field = va_arg (args, SFormat *);
    while (field)
    {
	g_queue_push_tail (fields, field);
	field = va_arg (args, SFormat *);
    }
    va_end (args);
    
    sformat->u.record.fields = fields;
    
    return sformat;
}

SFormat *
sformat_new_list (const char *name,
                  SFormat *content)
{
    SFormat *sformat = create_sformat (name, LIST);
    sformat->u.list.content = content;
    return sformat;
}

SFormat *
sformat_new_pointer (const char *name)
{
    SFormat *sformat = create_sformat (name, POINTER);
    return sformat;
}

SFormat *
sformat_new_integer (const char *name)
{
    SFormat *sformat = create_sformat (name, INTEGER);
    return sformat;
}

SFormat *
sformat_new_string (const char *name)
{
    SFormat *sformat = create_sformat (name, STRING);
    return sformat;
}

/* reading */
typedef struct BuildContext BuildContext;
typedef struct ParseNode ParseNode;

struct BuildContext
{
    ParseNode *root;
    ParseNode *current_node;
    GHashTable *nodes_by_id;
};

struct ParseNode
{
    ParseNode *parent;
    char *name;
    gpointer id;
    GPtrArray *children;
    GString *text;

    SFormat *format;
};

void
sfile_begin_get_record (SFile       *file)
{
    ReadItem *item = file->current_item++;

    g_return_if_fail (file->current_item - file->items < file->n_items);
    g_return_if_fail (item->action == BEGIN_RECORD);
}

int
sfile_begin_get_list   (SFile       *file)
{
    ReadItem *item = file->current_item++;

    g_return_val_if_fail (file->current_item - file->items < file->n_items, -1);
    g_return_val_if_fail (item->action == BEGIN_LIST, -1);

    return item->u.read_list.n_items;
}

void
sfile_get_pointer      (SFile       *file,
                        gpointer    *pointer)
{
    ReadItem *item = file->current_item++;

    g_return_if_fail (file->current_item - file->items < file->n_items);
    g_return_if_fail (item->action == READ_POINTER);

    g_hash_table_insert (file->locations_by_id, GINT_TO_POINTER (item->u.read_pointer.id), pointer);
}

void
sfile_get_integer      (SFile       *file,
                        int         *integer)
{
    ReadItem *item = file->current_item++;

    g_return_if_fail (file->current_item - file->items < file->n_items);
    g_return_if_fail (item->action == READ_INTEGER);

    *integer = item->u.read_integer.value;
}

void
sfile_get_string       (SFile       *file,
                        char       **string)
{
    ReadItem *item = file->current_item++;

    g_return_if_fail (file->current_item - file->items < file->n_items);
    g_return_if_fail (item->action == READ_STRING);

    *string = g_strdup (item->u.read_string.value);
}

static void
fixup_pointers (gpointer key, gpointer value, gpointer data)
{
    SFile *file = data;
    int id = GPOINTER_TO_INT (key);
    gpointer *location = value;

    *location = g_hash_table_lookup (file->objects_by_id, GINT_TO_POINTER (id));
}

void
sfile_end_get          (SFile       *file,
                        gpointer     object)
{
    ReadItem *item = file->current_item++;

    g_return_if_fail (file->current_item - file->items < file->n_items);
    g_return_if_fail (item->action == END);

    g_hash_table_insert (file->objects_by_id, GINT_TO_POINTER (item->u.end.id), object);

    if (file->current_item - file->items == file->n_items)
    {
        /* Nothing else to read. Fix up pointers */
        g_hash_table_foreach (file->locations_by_id, fixup_pointers, file);
    }
}

static gboolean
get_number (const char *text, int *number)
{
    char *end;
    int result;
    char *stripped;
    
    stripped = g_strstrip (g_strdup (text));
    result = strtol (stripped, &end, 10);
    g_free (stripped);
    
    if (*end != '\0')
	return FALSE;
    
    if (number)
	*number = result;
    
    return TRUE;
}

static ParseNode *
parse_node_new (ParseNode *parent,
                const char *name)
{
    ParseNode *node = g_new0 (ParseNode, 1);

    node->parent = parent;
    node->name = g_strdup (name);
    node->id = NULL;
    node->children = g_ptr_array_new ();
    node->text = g_string_new ("");

    if (parent)
        g_ptr_array_add (parent->children, node);
    
    return node;
}

static void
handle_begin_element (GMarkupParseContext *parse_context,
		      const gchar *element_name,
		      const gchar **attribute_names,
		      const gchar **attribute_values,
		      gpointer user_data,
		      GError **err)
{
    BuildContext *build = user_data;
    const char *id_string;
    ParseNode *node;
    int id;
    int i;

    /* Check for id */
    id_string = NULL;
    for (i = 0; attribute_names[i] != NULL; ++i)
    {
	if (strcmp (attribute_names[i], "id") == 0)
	{
	    if (id_string)
	    {
		/* FIXME: error: id defined twice */
		return;
	    }
	    else
	    {
		id_string = attribute_values[i];
                
                if (!get_number (id_string, &id) || id < 1)
                {
                    /* FIXME: bad attribute value for attribute 'id' */
                    return;
                }
	    }
	}
	else
	{
	    /* FIXME: unknown attribute */
	    return;
	}
    }

    if (build->current_node->text->len > 0)
    {
        /* FIXME: mixing children and text */
        return;
    }
    
    node = parse_node_new (build->current_node, element_name);

    if (id_string)
    {
        node->id = GINT_TO_POINTER (id);
        g_hash_table_insert (build->nodes_by_id, node->id, node);
    }

    build->current_node = node;

    if (!build->root)
        build->root = node;
}
    
static void
handle_end_element (GMarkupParseContext *context,
		    const gchar *element_name,
		    gpointer user_data,
		    GError **err)
{
    BuildContext *build = user_data;

    build->current_node = build->current_node->parent;
}

static void
handle_text (GMarkupParseContext *context,
	     const gchar *text,
             gsize text_len,
	     gpointer user_data,
	     GError **err)
{
    BuildContext *build = user_data;

    if (build->current_node->children->len > 0)
    {
        /* FIXME: set error: mixing children and text */
        return;
    }
    
    g_string_append (build->current_node->text, text);
}

static ParseNode *
build_tree (const char *text,
	    GHashTable *nodes_by_id,
	    GError **err)
{
    BuildContext build;
    GMarkupParseContext *parse_context;
    
    GMarkupParser parser = {
	handle_begin_element,
	handle_end_element,
	handle_text,
	NULL, /* passthrough */
	NULL, /* error */
    };

    build.root = NULL;
    build.current_node = NULL;
    build.nodes_by_id = nodes_by_id;
    
    parse_context = g_markup_parse_context_new (&parser, 0, &build, NULL);

    if (!g_markup_parse_context_parse (parse_context, text, -1, err))
    {
	/* FIXME: free stuff */
	return NULL;
    }

    if (!build.root)
    {
        /* FIXME: empty document not allowed */
        /* FIXME: free stuff */
        return NULL;
    }
    
    return build.root;
}

static gboolean check_structure (ParseNode *node, SFormat *format, GHashTable *nodes_by_id, GError **err);

static gboolean
check_list_node (ParseNode *list_node, SFormat *format, GHashTable *nodes_by_id, GError **err)
{
    SFormat *content = format->u.list.content;
    int i;
    
    for (i = 0; i < list_node->children->len; ++i)
    {
        ParseNode *child = list_node->children->pdata[i];
        
        if (!check_structure (child, content, nodes_by_id, err))
            return FALSE;
    }

    return TRUE;
}

static gboolean
check_pointer_node (ParseNode *pointer_node, SFormat *format, GHashTable *nodes_by_id, GError **err)
{
    int ref_id;

    if (!get_number (pointer_node->text->str, &ref_id))
    {
        /* Expected number */
        return FALSE;
    }

    if (g_hash_table_lookup (nodes_by_id, GINT_TO_POINTER (ref_id)))
    {
        /* Dangling pointer */
        return FALSE;
    }

    return TRUE;
}

static gboolean
check_integer_node (ParseNode *integer_node, SFormat *format, GHashTable *nodes_by_id, GError **err)
{
    if (!get_number (integer_node->text->str, NULL))
    {
        /* Expected number */
        return FALSE;
    }

    return TRUE;
}

static gboolean
check_string_node (ParseNode *parse_node, SFormat *format, GHashTable *nodes_by_id, GError **err)
{
    char *text = g_strdup (parse_node->text->str);

    g_strstrip (text);

    if (strlen (text) < 2)
    {
        /* FIXME: syntax error */
        return FALSE;
    }
    
    if (text[0] != '\"' || text[strlen (text) - 1] != '\"')
    {
        /* FIMXE: string not quoted */
        return FALSE;
    }

    g_free (text);

    return TRUE;
}

static gboolean
check_record_node (ParseNode *parse_node, SFormat *format, GHashTable *nodes_by_id, GError **err)
{
    GList *list;
    int i;
    
    if (parse_node->children->len != g_queue_get_length (format->u.record.fields))
    {
        /* FIXME: Set error: incorrect number of fields */
        return FALSE;
    }

    list = format->u.record.fields->head;
    for (i = 0; i < parse_node->children->len; ++i)
    {
        SFormat *field = list->data;
        ParseNode *child = parse_node->children->pdata[i];

        if (!check_structure (child, field, nodes_by_id, err))
            return FALSE;

        list = list->next;
    }

    return TRUE;
}

static gboolean
check_structure (ParseNode *parse_node, SFormat *format, GHashTable *nodes_by_id, GError **err)
{
    if (strcmp (parse_node->name, format->name) != 0)
    {
        /* FIXME: format->name expected */
        return FALSE;
    }

    parse_node->format = format;
    
    switch (format->type)
    {
    case LIST:
        return check_list_node (parse_node, format, nodes_by_id, err);
	break;
	
    case POINTER:
        return check_pointer_node (parse_node, format, nodes_by_id, err);
        break;
        
    case INTEGER:
        return check_integer_node (parse_node, format, nodes_by_id, err);
        break;
        
    case STRING:
        return check_string_node (parse_node, format, nodes_by_id, err);
	break;
	
    case RECORD:
        return check_record_node (parse_node, format, nodes_by_id, err);
        break;

    default:
        g_assert_not_reached ();
        return TRUE; /* quiet gcc */
    }
}

static void
create_read_list (ParseNode *tree, GArray *read_list)
{
    ReadItem item;
    int i;

    switch (tree->format->type)
    {
    case RECORD:
        item.action = BEGIN_RECORD;
        g_array_append_val (read_list, item);

        for (i = 0; i < tree->children->len; ++i)
            create_read_list (tree, read_list);

        item.action = END;
        g_array_append_val (read_list, item);
        break;
        
    case LIST:
        item.action = BEGIN_LIST;
        g_array_append_val (read_list, item);
        
        for (i = 0; i < tree->children->len; ++i)
            create_read_list (tree, read_list);

        item.action = END;
        g_array_append_val (read_list, item);
        break;

    case POINTER:
        item.action = READ_POINTER;
        get_number (tree->text->str, &item.u.read_pointer.id);
        break;

    case INTEGER:
        item.action = READ_INTEGER;
        get_number (tree->text->str, &item.u.read_integer.value);
        break;

    case STRING:
        item.action = READ_STRING;
        /* Copy string without quotation marks */
        item.u.read_string.value =
            g_strndup (tree->text->str + 1, strlen (tree->text->str) - 2);
        break;
    }
}

static void
parse_node_free (ParseNode *node)
{
    int i;

    for (i = 0; i < node->children->len; ++i)
        parse_node_free (node);
    
    g_free (node->name);
    g_ptr_array_free (node->children, TRUE);
    g_string_free (node->text, TRUE);
    
}

SFile *
sfile_load (const char  *filename,
	    SFormat       *format,
	    GError     **err)
{
    gchar *contents;
    gsize length;
    ParseNode *tree;
    GHashTable *nodes_by_id;
    GArray *read_list;
    SFile *sfile = g_new0 (SFile, 1);
    
    if (!g_file_get_contents (filename, &contents, &length, err))
	return NULL;
    
    nodes_by_id = g_hash_table_new (g_direct_hash, g_direct_equal);

    tree = build_tree (contents, nodes_by_id, err);
    if (!tree)
    {
	/* FIXME: free stuff */
	return NULL;
    }

    if (!check_structure (tree, format, nodes_by_id, err))
    {
	/* FIXME: free stuff */
	return NULL;
    }

    read_list = g_array_new (TRUE, TRUE, sizeof (ReadItem));
    
    create_read_list (tree, read_list);

    sfile->n_items = read_list->len;
    sfile->items = (ReadItem *)g_array_free (read_list, FALSE);
    sfile->current_item = sfile->items;
    sfile->objects_by_id = g_hash_table_new (g_direct_hash, g_direct_equal);
    sfile->locations_by_id = g_hash_table_new (g_direct_hash, g_direct_equal);

    parse_node_free (tree);
    
    return sfile;
}

/* A transisition can start or end in the 'external state'.
 * This state just represents something outside the machine.
 * There is one transition *from* the external state, and
 * one transition *to* the external state.
 */

#define EXTERNAL_STATE  (-1)

struct Transition
{
    int from, to;
    
    enum { BEGIN, END, TEXT } type;
    char *element;
};

struct StateMachine
{
    int n_transitions;
    Transition *transitions;
};

static void
get_machine_info (StateMachine *machine,
                  Transition **enter,
                  Transition **exit,
                  int *n_states)
{
    
}

static void
make_list_machine (State *external, State *content, )
{
    int n_transitions = content->n_transitions;
    int n_states, new_state;
    Transition *enter_trans;
    Transition *exit_trans;

    get_machine_info (content, &n_states, &enter_trans, &exit_trans);

    new_state = n_states;
    
    content->transitions = g_renew (Transition, content->transitions, n_transitions + 2);

    content->transitions[n_transitions].from = EXTERNAL_STATE;
    content->transitions[n_transitions].to = n_states;
    content->transitions[n_transitions].type = BEGIN;
    content->transitions[n_transitions].element = g_strdup ("froot");

    content->transitions[n_transitions + 1].to = EXTERNAL_STATE;
    content->transitions[n_transitions + 1].from = n_states;
    content->transitions[n_transitions + 1].type = END;
    content->transitions[n_transitions + 1].element = g_strdup ("froot");
    
    enter_trans->from = new_state;
    exit_trans->to = new_state;
}

static void
chain_machines (GList *machines, int *enter_state, int *exit_state)
{
}

static void
make_record_machine (GQueue *fields)
{
    Transition *enter_trans;
    int from = EXTERNAL_STATE;
    GQueue *state_chain = g_queue_new ();

    g_queue_push_tail (state_chain, EXTERNAL_STATEA);

    for (list = fields->head; list; list = list->next)
    {
        StateMachine *machine = list->data;
        Transition *enter_trans;
        Transition *exit_trans;

        get_machine_info (machine, NULL, &enter_trans, &exit_trans);
        enter_trans->from = from;

        if (list->next)
            get_machine_info (list->next->data, NULL, NULL, next_enterenter_trans->to = to;
    }
}
