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

typedef struct State State;
typedef struct Transition Transition;
typedef struct Fragment Fragment;
typedef struct Action Action;

struct SFormat
{
    State *begin;
    State *end;
};

/* defining types */
typedef enum
{
    BEGIN_RECORD,
    BEGIN_LIST,
    END_RECORD,
    END_LIST,
    BEGIN_POINTER,
    END_POINTER,
    BEGIN_INTEGER,
    END_INTEGER,
    BEGIN_STRING,
    END_STRING,
    POINTER,
    INTEGER,
    STRING
} TransitionType;

struct Transition
{
    TransitionType type;
    State *to;
    char *element;              /* for begin/end transitions */
};

struct State
{
    GQueue *transitions;
};

struct Fragment
{
    Transition *enter, *exit;
};

struct Action
{
};

static State *
state_new (void)
{
    State *state = g_new (State, 1);
    state->transitions = g_queue_new ();
    return state;
}

static Transition *
transition_new_begin_union (const char *name, State *from, State *to)
{
    return NULL; /* FIXME */
}

static Transition *
transition_new_end_union (const char *name, State *from, State *to)
{
    return NULL; /* FIXME */
}

static Transition *
transition_new_begin_record (const char *name, State *from, State *to)
{
    return NULL; /* FIXME */
}

static Transition *
transition_new_end_record (const char *name, State *from, State *to)
{
    return NULL; /* FIXME */
}

static Transition *
transition_new_begin_pointer (const char *name, State *from, State *to)
{
    return NULL; /* FIXME */
}

static Transition *
transition_new_end_pointer (const char *name, State *from, State *to)
{
    return NULL; /* FIXME */
}

static Transition *
transition_new_begin_integer (const char *name, State *from, State *to)
{
    return NULL; /* FIXME */
}

static Transition *
transition_new_end_integer (const char *name, State *from, State *to)
{
    return NULL; /* FIXME */
}

static Transition *
transition_new_begin_string (const char *name, State *from, State *to)
{
    return NULL; /* FIXME */
}

static Transition *
transition_new_end_string (const char *name, State *from, State *to)
{
    return NULL; /* FIXME */
}

static Transition *
transition_new_begin_list (const char *name, State *from, State *to)
{
    return NULL; /* FIXME */
}

static Transition *
transition_new_end_list (const char *name, State *from, State *to)
{
    return NULL; /* FIXME */
}

static Transition *
transition_new_begin_value (const char *name, TransitionType type, State *from, State *to)
{
    return NULL; /* FIXME */
}

static Transition *
transition_new_end_value (const char *name, TransitionType type, State *from, State *to)
{
    return NULL; /* FIXME */
}

static Transition *
transition_new_value (TransitionType type, State *from, State *to)
{
    return NULL; /* FIXME */
}

SFormat *
sformat_new (gpointer f)
{
    SFormat *sformat = g_new0 (SFormat, 1);
    Fragment *fragment = f;

    sformat->begin = state_new ();
    sformat->end = state_new ();

    g_queue_push_tail (sformat->begin->transitions, fragment->enter);
    fragment->exit->to = sformat->end;

    g_free (fragment);
    
    return sformat;
}

static GQueue *
fragment_queue (va_list args)
{
    GQueue *fragments = g_queue_new ();
    Fragment *fragment;
    
    fragment = va_arg (args, Fragment *);
    while (fragment)
    {
	g_queue_push_tail (fragments, fragment);
	fragment = va_arg (args, Fragment *);
    }
    va_end (args);

    return fragments;
}

/* Consider adding unions at some point
 *
 * To be useful they should probably be anonymous, so that
 * the union itself doesn't have a representation in the
 * xml file.
 *
 *     API:
 *            sformat_new_union (gpointer content1, ...);
 *
 *            char *content = begin_get_union ();
 *            if (strcmp (content, ...) == 0)
 *                      get_pointer ();
 *            else if (strcmp (content, ...) == 0)
 *
 *               ;
 *
 * Annoying though, that we then won't have the nice one-to-one
 * correspondence between begin()/end() calls and <element></element>s
 * Actually, we will probably have to have <union>asdlfkj</union>
 * elements. That will make things a lot easier, and unions are
 * still pretty useful if you put big things like lists in them.
 * 
 * We may also consider adding anonymous records. These will
 * not be able to have pointers associated with them though
 * (because there wouldn't be a natural place 
 */

gpointer
sformat_new_union (const char *name,
                   gpointer content1,
                   ...)
{
    va_list args;
    GQueue *fragments;
    GList *list;
    Fragment *fragment;
    Transition *enter, *exit;
    State *begin;
    State *end;

    va_start (args, content1);

    fragments = fragment_queue (args);
    
    va_end (args);

    begin = state_new ();
    end = state_new ();

    enter = transition_new_begin_union (name, NULL, begin);
    exit = transition_new_end_union (name, end, NULL);
    
    for (list = fragments->head; list; list = list->next)
    {
        Fragment *fragment = list->data;

        g_queue_push_tail (begin->transitions, fragment->enter);
    }

    for (list = fragments->head; list; list = list->next)
    {
        fragment = list->data;

        fragment->exit->to = end;

        g_free (fragment);
    }

    g_queue_free (fragments);

    fragment = g_new (Fragment, 1);
    fragment->enter = enter;
    fragment->exit = exit;

    return fragment;
}

gpointer
sformat_new_record  (const char *     name,
                     gpointer         content1,
                     ...)
{
    va_list args;
    GQueue *fragments;
    Transition *enter;
    State *begin, *state;
    Fragment *fragment;
    GList *list;

    /* Build queue of fragments */
    va_start (args, content1);

    fragments = fragment_queue (args);

    va_end (args);
    
    /* chain fragments together */
    begin = state_new ();
    enter = transition_new_begin_record (name, NULL, begin);
    
    state = begin;

    for (list = fragments->head; list != NULL; list = list->next)
    {
        /* At each point in the iteration we need,
         *    - a target machine (in list->data)
         *    - a state that will lead into that machine (in state)
         */
        fragment = list->data;

        g_queue_push_tail (state->transitions, fragment->enter);

        state = state_new ();
        fragment->exit->to = state;
    }

    /* Return resulting fragment */
    fragment = g_new (Fragment, 1);
    fragment->enter = enter;
    fragment->exit = transition_new_end_record (name, state, NULL);
    
    return fragment;
}

gpointer
sformat_new_list (const char *name,
                  gpointer content)
{
    Fragment *m = content;
    State *list_state;
    Transition *enter, *exit;

    list_state = state_new ();
    
    enter = transition_new_begin_list (name, NULL, list_state);
    exit = transition_new_end_list (name, list_state, NULL);
    
    g_queue_push_tail (list_state->transitions, m->enter);
    m->exit->to = list_state;

    m->enter = enter;
    m->exit = exit;

    return m;
}

gpointer
sformat_new_value (const char *name, TransitionType type)
{
    Fragment *m = g_new (Fragment, 1);
    State *before, *after;
    Transition *value;

    before = state_new ();
    after = state_new ();

    m->enter = transition_new_begin_value (name, type, NULL, before);
    m->exit  = transition_new_end_value   (name, type, after, NULL);
    value = transition_new_value          (type, before, after);
    
    return m;
}

gpointer
sformat_new_pointer (const char *name)
{
    return sformat_new_value (name, POINTER);
}

gpointer
sformat_new_integer (const char *name)
{
    return sformat_new_value (name, INTEGER);
}

gpointer
sformat_new_string (const char *name)
{
    return sformat_new_value (name, STRING);
}

static const State *
sformat_get_start_state (SFormat *format)
{
    return format->begin;
}

static gboolean
sformat_is_end_state (SFormat *format, const State *state)
{
    return format->end == state;
}

static const State *
state_transition_begin (const State *state, const char *element,
                        TransitionType *type, GError **err)
{
    return NULL; /* FIXME */
}

static const State *
state_transition_end (const State *state, const char *element,
                      TransitionType *type, GError **err)
{
    return NULL; /* FIXME */
}

static const State *
state_transition_text (const State *state, const char *element,
                       TransitionType *type, GError **err)
{
    return NULL; /* FIXME */
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
sfile_begin_get_record (SFileInput       *file)
{
}

int
sfile_begin_get_list   (SFileInput       *file)
{
    return -1; /* FIXME */
}

void
sfile_get_pointer      (SFileInput       *file,
                        gpointer    *pointer)
{
    
}

void
sfile_get_integer      (SFileInput       *file,
                        int         *integer)
{
}

void
sfile_get_string       (SFileInput       *file,
                        char       **string)
{
}

void
sfile_end_get          (SFileInput       *file,
                        gpointer     object)
{
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


struct SFileInput
{
    int n_actions;
    Action *actions;
};

static Action *
build_actions (const char *contents, int *n_actions, GError **err)
{
    return NULL; /* FIXME */
}

SFileInput *
sfile_load (const char  *filename,
	    SFormat       *format,
	    GError     **err)
{
    gchar *contents;
    gsize length;
    ParseNode *tree;
    GHashTable *nodes_by_id;
    GArray *read_list;
    SFileInput *input;
    
    if (!g_file_get_contents (filename, &contents, &length, err))
	return NULL;

    input = g_new (SFileInput, 1);
    
    input->actions = build_actions (contents, &input->n_actions, err);

    if (!input->actions)
    {
        g_free (input);
        g_free (contents);
        return NULL;
    }

    g_free (contents);
    
    return input;
}
