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
#include <string.h>

#include "sformat.h"

typedef struct State State;
typedef struct Transition Transition;
typedef struct Fragment Fragment;

/*
 * Format
 */
struct SFormat
{
    State *	begin;
    State *	end;
    
    GQueue *	types;
    GQueue *	transitions;
    GQueue *	states;
};

/*
 * Type
 */
typedef enum
{
    TYPE_POINTER,
    TYPE_STRING,
    TYPE_INTEGER,
    TYPE_RECORD,
    TYPE_LIST,
    TYPE_FORWARD
} TypeKind;

struct SType
{
    TypeKind	kind;
    char       *name;
    Transition *enter, *exit;
    SType      *target;		    /* If kind is TYPE_POINTER */
};

static void type_free (SType *type);

/*
 * Transition
 */
typedef enum
{
    BEGIN,
    VALUE,
    END
} TransitionKind;

struct Transition
{
    SType *		type;
    TransitionKind	kind;
    State *		to;
};

static Transition *transition_new   (SFormat        *format,
				     TransitionKind  kind,
				     SType          *type,
				     State          *from,
				     State          *to);
static void transition_free         (Transition     *transition);
static void transition_set_to_state (Transition     *transition,
				     State          *to_state);

/*
 * State
 */ 

struct State
{
    GQueue *transitions;
};

static State *state_new            (SFormat    *format);
static void   state_add_transition (State      *state,
				    Transition *transition);
static void   state_free           (State      *state);

/*
 * Format
 */
SFormat *
sformat_new (void)
{
    /* FIXME: should probably be refcounted, and an SContext
     * should have a ref on the format
     */
    SFormat *format = g_new0 (SFormat, 1);
    
    format->begin = NULL;
    format->end = NULL;
    
    format->types = g_queue_new ();
    format->states = g_queue_new ();
    format->transitions = g_queue_new ();
    
    return format;
}

void
sformat_free (SFormat *format)
{
    GList *list;
    
    for (list = format->types->head; list; list = list->next)
    {
	SType *type = list->data;
	
	type_free (type);
    }
    g_queue_free (format->types);
    
    for (list = format->states->head; list; list = list->next)
    {
	State *state = list->data;
	
	state_free (state);
    }
    g_queue_free (format->states);
    
    for (list = format->transitions->head; list; list = list->next)
    {
	Transition *transition = list->data;
	
	transition_free (transition);
    }
    g_queue_free (format->transitions);
    
    g_free (format);
}

void
sformat_set_type (SFormat *format,
		  SType   *type)
{
    format->begin = state_new (format);
    format->end = state_new (format);
    
    state_add_transition (format->begin, type->enter);
    transition_set_to_state (type->exit, format->end);
}

/*
 * Type
 */
static SType *
type_new (SFormat *format,
	  TypeKind kind,
	  const char *name)
{
    SType *type = g_new0 (SType, 1);
    
    type->kind = kind;
    type->name = name? g_strdup (name) : NULL;
    type->enter = NULL;
    type->exit = NULL;
    type->target = NULL;
    
    g_queue_push_tail (format->types, type);
    
    return type;
}

static SType *
type_new_from_forward (SFormat *format,
		       TypeKind kind,
		       const char *name,
		       SForward *forward)
{
    SType *type;
    
    if (forward)
    {
	type = (SType *)forward;
	type->kind = kind;
	type->name = g_strdup (name);
    }
    else
    {
	type = type_new (format, kind, name);
    }
    
    return type;
}


static SType *
type_new_value (SFormat *format, TypeKind kind, const char *name)
{
    SType *type = type_new (format, kind, name);
    State *before, *after;
    Transition *value;
    
    before = state_new (format);
    after = state_new (format);
    
    type->enter = transition_new (format, BEGIN, type, NULL, before);
    type->exit  = transition_new (format, END, type, after, NULL);
    value = transition_new (format, VALUE, type, before, after);
    
    return type;
}

static void
type_free (SType *type)
{
    g_free (type->name);
    g_free (type);
}

SForward *
sformat_declare_forward (SFormat *format)
{
    SType *type = type_new (format, TYPE_FORWARD, NULL);
    
    return (SForward *)type;
}


static GQueue *
expand_varargs (SType *content1,
		va_list args)
{
    GQueue *types = g_queue_new ();
    SType *type;
    
    g_queue_push_tail (types, content1);
    
    type = va_arg (args, SType *);
    while (type)
    {
	g_queue_push_tail (types, type);
	type = va_arg (args, SType *);
    }
    
    return types;
}

SType *
sformat_make_record (SFormat *format,
		     const char *name,
		     SForward *forward,
		     SType *content,
		     ...)
{
    SType *type;
    va_list args;
    GQueue *types;
    GList *list;
    State *begin, *state;
    
    /* Build queue of child types */
    va_start (args, content);
    types = expand_varargs (content, args);
    va_end (args);
    
    /* chain types together */
    state = begin = state_new (format);
    
    for (list = types->head; list != NULL; list = list->next)
    {
        SType *child_type = list->data;
	
        state_add_transition (state, child_type->enter);
	
        state = state_new (format);
	
	transition_set_to_state (child_type->exit, state);
    }
    
    g_queue_free (types);
    
    /* create and return the new type */
    type = type_new_from_forward (format, TYPE_RECORD, name, forward);
    type->enter = transition_new (format, BEGIN, type, NULL, begin);
    type->exit = transition_new (format, END, type, state, NULL);
    
    return type;
}

SType *
sformat_make_list   (SFormat *format,
		     const char *name,
		     SForward *forward,
		     SType   *child_type)
{
    SType *type;
    State *list_state;

    type = type_new_from_forward (format, TYPE_LIST, name, forward);

    list_state = state_new (format);
    
    type->enter = transition_new (format, BEGIN, type, NULL, list_state);
    type->exit = transition_new (format, END, type, list_state, NULL);
    
    state_add_transition (list_state, child_type->enter);
    transition_set_to_state (child_type->exit, list_state);
    
    return type;
}

SType *
sformat_make_pointer (SFormat     *format,
		      const char  *name,
		      SForward    *forward)
{
    SType *type = type_new_value (format, TYPE_POINTER, name);
    type->target = (SType *)forward;
    
    return type;
}

SType *
sformat_make_integer (SFormat *format,
		      const    char *name)
{
    return type_new_value (format, TYPE_INTEGER, name);
}

SType *
sformat_make_string  (SFormat *format,
		      const    char *name)
{
    return type_new_value (format, TYPE_STRING, name);
}



gboolean
stype_is_record (SType *type)
{
    return type->kind == TYPE_RECORD;
}

gboolean
stype_is_list (SType *type)
{
    return type->kind == TYPE_LIST;
}

gboolean
stype_is_pointer (SType *type)
{
    return type->kind == TYPE_POINTER;
}

gboolean
stype_is_integer (SType *type)
{
    return type->kind == TYPE_INTEGER;
}

gboolean
stype_is_string (SType *type)
{
    return type->kind == TYPE_STRING;
}

SType *
stype_get_target_type (SType *type)
{
    g_return_val_if_fail (stype_is_pointer (type), NULL);
    
    return type->target;
}

const char *
stype_get_name (SType *type)
{
    return type->name;
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
 * Or maybe just give them a name ...
 *
 * We may also consider adding anonymous records. These will
 * not be able to have pointers associated with them though
 * (because there wouldn't be a natural place
 *
 *
 * Also consider adding the following data types:
 *
 * - Binary blobs of data, stored as base64 perhaps
 * 
 * - floating point values. How do we store those portably
 *     without losing precision? Gnumeric may know.
 * 
 * - enums, stored as strings
 *
 * - booleans.
 */


/*
 * State
 */
static State *
state_new (SFormat *format)
{
    State *state = g_new0 (State, 1);
    state->transitions = g_queue_new ();
    
    g_queue_push_tail (format->states, state);
    
    return state;
}

static void
state_add_transition (State *state,
		      Transition *transition)
{
    g_queue_push_tail (state->transitions, transition);
}

static void
state_free (State *state)
{
    g_queue_free (state->transitions);
    g_free (state);
}

/*
 * Transition
 */
static Transition *
transition_new (SFormat *format,
		TransitionKind kind,
		SType *type,
		State *from,
		State *to)
{
    Transition *transition = g_new0 (Transition, 1);
    
    transition->type = type;
    transition->kind = kind;
    transition->to = to;
    
    if (from)
	state_add_transition (from, transition);
    
    g_queue_push_tail (format->transitions, transition);
    
    return transition;
}

static void
transition_free (Transition *transition)
{
    g_free (transition);
}

static void
transition_set_to_state (Transition *transition,
			 State      *to_state)
{
    transition->to = to_state;
}

/* Context */
struct SContext
{
    SFormat *format;
    State *state;
};

SContext *
scontext_new (SFormat *format)
{
    SContext *context = g_new0 (SContext, 1);
    
    context->format = format;
    context->state = format->begin;
    
    return context;
}

static SType *
do_transition (SContext *context,
	       TransitionKind kind,
	       const char *element)
{
    GList *list;
    
    for (list = context->state->transitions->head; list; list = list->next)
    {
	Transition *transition = list->data;
	
	if (transition->kind == kind)
	{
	    if (kind == VALUE || strcmp (transition->type->name, element) == 0)
	    {
		context->state = transition->to;
		return transition->type;
	    }
	}
    }
    
    return NULL;
}

SType *
scontext_begin (SContext *context,
		const char *element)
{
    return do_transition (context, BEGIN, element);
}

SType *
scontext_text (SContext *context)
{
    return do_transition (context, VALUE, NULL);
}

SType *
scontext_end (SContext *context,
	      const char *element)
{
    return do_transition (context, END, element);
}

gboolean
scontext_is_finished (SContext *context)
{
    return context->state == context->format->end;
}

void
scontext_free (SContext *context)
{
    g_free (context);
}



/* assorted stuff */
#if 0
static const State *
state_transition_check (const State *state,
                        const char *element,
                        TransitionKind kind,
                        SType *type)
{
    GList *list;
    
    for (list = state->transitions->head; list; list = list->next)
    {
        Transition *transition = list->data;
	
        if (transition->kind == kind                    &&
            strcmp (element, transition->element) == 0)
        {
            *type = transition->type;
            return transition->to;
        }
    }
    
    return NULL;
}

static const State *
state_transition_begin (const State *state, const char *element, SType *type)
{
    return state_transition_check (state, element, BEGIN, type);
}

static const State *
state_transition_end (const State *state, const char *element, SType *type)
{
    return state_transition_check (state, element, END, type);
}

static const State *
state_transition_text (const State *state, SType *type, SType *target_type)
{
    GList *list;
    
    for (list = state->transitions->head; list; list = list->next)
    {
        Transition *transition = list->data;
	
        if (transition->kind == VALUE)
        {
            *type = transition->type;
	    
	    if (*type == TYPE_POINTER && target_type)
		*target_type = transition->target_type;
	    
            /* There will never be more than one allowed value transition for
             * a given state
             */
            return transition->to;
        }
    }
    
    return NULL;
}

#endif
