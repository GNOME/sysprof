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

/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*- */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#if 0
#include <bzlib.h>
#endif
#include "sfile.h"

typedef struct State State;
typedef struct Transition Transition;
typedef struct Fragment Fragment;

struct SFormat
{
    State *begin;
    State *end;
};

enum
{
    TYPE_UNDEFINED = 0,
    TYPE_POINTER,
    TYPE_STRING,
    TYPE_INTEGER,
    TYPE_GENERIC_RECORD,
    TYPE_GENERIC_LIST,
    TYPE_VOID,
    N_BUILTIN_TYPES,
};

typedef enum
{
    BEGIN,
    VALUE,
    END
} TransitionKind;

struct Transition
{
    SType type;
    TransitionKind kind;
    State *to;
    char *element;         /* for begin/end transitions */
    SType target_type;	   /* for pointer transitions */
};

struct State
{
    GQueue *transitions;
};

struct Fragment
{
    Transition *enter, *exit;
};

static void
set_error (GError **err, gint code, const char *format, va_list args)
{
    char *msg;
    
    if (!err)
        return;
    
    msg = g_strdup_vprintf (format, args);
    
    if (*err == NULL)
    {
        *err = g_error_new_literal (G_MARKUP_ERROR, code, msg);
    }
    else
    {
        /* Warning text from GLib */
        g_warning ("GError set over the top of a previous GError or uninitialized memory.\n" 
                   "This indicates a bug in someone's code. You must ensure an error is NULL before it's set.\n"
                   "The overwriting error message was: %s",
                   msg);
    }
    
    g_free (msg);
}

static void
set_unknown_element_error (GError **err, const char *format, ...)
{
    va_list args;
    va_start (args, format);
    set_error (err, G_MARKUP_ERROR_UNKNOWN_ELEMENT, format, args);
    va_end (args);
}

static void
set_unknown_attribute_error (GError **err, const char *format, ...)
{
    va_list args;
    va_start (args, format);
    set_error (err, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE, format, args);
    va_end (args);
}

static void
set_invalid_content_error (GError **err, const char *format, ...)
{
    va_list args;
    va_start (args, format);
    set_error (err, G_MARKUP_ERROR_INVALID_CONTENT, format, args);
    va_end (args);
}

static Transition *
transition_new (const char *element,
                TransitionKind kind,
                SType type,
                State *from,
                State *to)
{
    Transition *t = g_new (Transition, 1);

    g_assert (element || kind == VALUE);
    
    t->element = element? g_strdup (element) : NULL;
    t->kind = kind;
    t->type = type;
    t->to = to;
    t->target_type = TYPE_UNDEFINED;
    
    if (from)
        g_queue_push_tail (from->transitions, t);
    
    return t;
}

static void
transition_free (Transition *transition)
{
    if (transition->element)
	g_free (transition->element);
    g_free (transition);
}

static State *
state_new (void)
{
    State *state = g_new (State, 1);
    state->transitions = g_queue_new ();
    return state;
}

static void
state_free (State *state)
{
    GList *list;
    
    for (list = state->transitions->head; list; list = list->next)
    {
	Transition *transition = list->data;

	transition_free (transition);
    }
    
    g_queue_free (state->transitions);
    g_free (state);
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

#if 0
SFormat *
sformat_new_optional (gpointer f)
{
    SFormat *sformat = g_new0 (SFormat, 1);
    Fragment *fragment = f;

    sformat->begin = state_new ();
    sformat->end = state_new ();
}
#endif

static void
add_state (State *state, GHashTable *seen_states, GQueue *todo_list)
{
    if (!g_hash_table_lookup (seen_states, state))
    {
	g_hash_table_insert (seen_states, state, state);
	g_queue_push_tail (todo_list, state);
    }
}

void
sformat_free (SFormat *format)
{
    GHashTable *seen_states = g_hash_table_new (g_direct_hash, g_direct_equal);
    GQueue *todo_list = g_queue_new ();
    
    add_state (format->begin, seen_states, todo_list);
    add_state (format->end, seen_states, todo_list);

    while (!g_queue_is_empty (todo_list))
    {
	GList *list;
	State *state = g_queue_pop_head (todo_list);

	for (list = state->transitions->head; list != NULL; list = list->next)
	{
	    Transition *transition = list->data;
	    add_state (transition->to, seen_states, todo_list);
	}

	state_free (state);
    }
    
    g_hash_table_destroy (seen_states);
    g_queue_free (todo_list);
}

static GQueue *
fragment_queue (Fragment *fragment1, va_list args)
{
    GQueue *fragments = g_queue_new ();
    Fragment *fragment;

    g_queue_push_tail (fragments, fragment1);
    
    fragment = va_arg (args, Fragment *);
    while (fragment)
    {
	g_queue_push_tail (fragments, fragment);
	fragment = va_arg (args, Fragment *);
    }
    
    return fragments;
}

#if 0
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
 *
 *
 * Also consider adding the following data types:
 *
 * Binary blobs of data, stored as base64 perhaps
 * floating point values. How do we store those portably
 *     without losing precision? Gnumeric may know.
 * enums, stored as strings
 * booleans
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
    
    enter = transition_new (name, TRANSITION_BEGIN_UNION, NULL, begin);
    exit = transition_new (name, TRANSITION_END_UNION, end, NULL);
    
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
#endif

#define RECORD_SHIFT	(sizeof (SType) * 8 - 1)
#define LIST_SHIFT	(sizeof (SType) * 8 - 2)
    
static SType
define_type (SType *type, SType fallback)
{
    static SType type_ids = N_BUILTIN_TYPES;
    
    if (type)
    {
        if (*type == 0)
            *type = type_ids++;

        return *type;
    }
    
    return fallback;
}

static gboolean
is_record_type (SType type)
{
    /* FIMXE - not10 */
    return TRUE;
}

static gboolean
is_list_type (SType type)
{
    /* FIXME - not10 */
    return TRUE;
}

gpointer
sformat_new_record  (const char *     name,
                     SType           *type,
                     gpointer         content1,
                     ...)
{
    va_list args;
    GQueue *fragments;
    State *begin, *state;
    Fragment *fragment;
    GList *list;
    SType real_type;

    /* Build queue of fragments */
    va_start (args, content1);
    
    fragments = fragment_queue (content1, args);
    
    va_end (args);
    
    /* chain fragments together */
    state = begin = state_new ();
    
    for (list = fragments->head; list != NULL; list = list->next)
    {
        fragment = list->data;
        
        g_queue_push_tail (state->transitions, fragment->enter);
        
        state = state_new ();
        fragment->exit->to = state;
    }

    real_type = define_type (type, TYPE_GENERIC_RECORD);
    
    /* Return resulting fragment */
    fragment = g_new (Fragment, 1);
    fragment->enter = transition_new (name, BEGIN, real_type, NULL, begin);
    fragment->exit = transition_new (name, END, real_type, state, NULL);
    
    return fragment;
}

gpointer
sformat_new_list (const char *name,
                  SType *type,
                  gpointer content)
{
    Fragment *m = content;
    State *list_state;
    Transition *enter, *exit;
    SType real_type;
    
    list_state = state_new ();

    real_type = define_type (type, TYPE_GENERIC_LIST);
    
    enter = transition_new (name, BEGIN, real_type, NULL, list_state);
    exit = transition_new (name, END, real_type, list_state, NULL);
    
    g_queue_push_tail (list_state->transitions, m->enter);
    m->exit->to = list_state;
    
    m->enter = enter;
    m->exit = exit;
    
    return m;
}

static gpointer
sformat_new_value (const char *name,
                   SType type)
{
    Fragment *m = g_new (Fragment, 1);
    State *before, *after;
    Transition *value;
    
    before = state_new ();
    after = state_new ();
    
    m->enter = transition_new (name, BEGIN, type, NULL, before);
    m->exit  = transition_new (name, END, type, after, NULL);
    value = transition_new (NULL, VALUE, type, before, after);
    
    return m;
}

gpointer
sformat_new_pointer (const char *name,
                     SType      *target_type)
{
    Fragment *fragment = sformat_new_value (name, TYPE_POINTER);
    Transition *value;

    /* store the target type in the value transition */ 
    value = fragment->enter->to->transitions->head->data;
    value->target_type = define_type (target_type, TYPE_VOID);
    
    return fragment;
}

gpointer
sformat_new_integer (const char *name)
{
    return sformat_new_value (name, TYPE_INTEGER);
}

gpointer
sformat_new_string (const char *name)
{
    return sformat_new_value (name, TYPE_STRING);
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
#if 0
        else
            g_print ("transition: %d (%s)\n", transition->kind, transition->element);
#endif
    }
    
    return NULL;
}

/* reading */
typedef struct BuildContext BuildContext;
typedef struct Instruction Instruction;

struct Instruction
{
    TransitionKind kind;
    SType type;
    
    char *name;
    union
    {
        struct
        {
	    gboolean is_list;
	    gboolean is_record;
            int n_elements;
            int id;
            Instruction *end_instruction;
        } begin;
        
        struct
        {
            Instruction *begin_instruction;
            gpointer object;
        } end;
        
        struct
        {
	    SType target_type;
            int target_id;
            Instruction *target_instruction;
            gpointer target_object;
            gpointer *location;
        } pointer;
        
        struct
        {
            int value;
        } integer;
        
        struct
        {
            char *value;
        } string;
    } u; 
};

struct BuildContext
{
    const State *state;

    GArray *instructions;
};

static gboolean
get_number (const char *text, int *number)
{
    char *end;
    int result;
    char *stripped;
    gboolean retval;

    stripped = g_strstrip (g_strdup (text));
    result = strtol (stripped, &end, 10);

    retval = (*end == '\0');

    if (retval && number)
        *number = result;
    
    g_free (stripped);
    
    return retval;
}

struct SFileInput
{
    int n_instructions;
    Instruction *instructions;
    Instruction *current_instruction;
    GHashTable *instructions_by_location;
};

void
sfile_begin_get_record (SFileInput *file, const char *name)
{
    Instruction *instruction = file->current_instruction++;

    g_return_if_fail (instruction->kind == BEGIN);
    g_return_if_fail (strcmp (instruction->name, name) == 0);
    g_return_if_fail (is_record_type (instruction->type));
}

int
sfile_begin_get_list   (SFileInput       *file,
                        const char   *name)
{
    Instruction *instruction = file->current_instruction++;
    
    g_return_val_if_fail (instruction->kind == BEGIN, 0);
    g_return_val_if_fail (strcmp (instruction->name, name) == 0, 0);
    g_return_val_if_fail (is_list_type (instruction->type), 0);
    
    return instruction->u.begin.n_elements;
}

void
sfile_get_pointer (SFileInput  *file,
                   const char *name,
                   gpointer    *location)
{
    Instruction *instruction;

    instruction = file->current_instruction++;
    g_return_if_fail (instruction->type == TYPE_POINTER &&
                      strcmp (instruction->name, name) == 0);
    
    instruction = file->current_instruction++;
    g_return_if_fail (instruction->type == TYPE_POINTER);
    
    instruction->u.pointer.location = location;

    *location = (gpointer) 0xFedeAbe;

    if (location)
    {
        if (g_hash_table_lookup (file->instructions_by_location, location))
            g_warning ("Reading into the same location twice\n");
        
        g_hash_table_insert (file->instructions_by_location, location, instruction);
    }

    instruction = file->current_instruction++;
    g_return_if_fail (instruction->type == TYPE_POINTER &&
                      strcmp (instruction->name, name) == 0);
    
}

void
sfile_get_integer      (SFileInput  *file,
                        const char  *name,
                        gint32      *integer)
{
    Instruction *instruction;

    instruction = file->current_instruction++;
    g_return_if_fail (instruction->type == TYPE_INTEGER &&
                      strcmp (instruction->name, name) == 0);
    
    instruction = file->current_instruction++;
    g_return_if_fail (instruction->type == TYPE_INTEGER);
    
    if (integer)
        *integer = instruction->u.integer.value;

    instruction = file->current_instruction++;
    g_return_if_fail (instruction->type == TYPE_INTEGER &&
                      strcmp (instruction->name, name) == 0);
}

void
sfile_get_string       (SFileInput  *file,
                        const char  *name,
                        char       **string)
{
    Instruction *instruction;

    instruction = file->current_instruction++;
    g_return_if_fail (instruction->type == TYPE_STRING &&
                      strcmp (instruction->name, name) == 0);
    
    instruction = file->current_instruction++;
    g_return_if_fail (instruction->type == TYPE_STRING);
    
    if (string)
        *string = g_strdup (instruction->u.string.value);

    instruction = file->current_instruction++;
    g_return_if_fail (instruction->type == TYPE_STRING &&
                      strcmp (instruction->name, name) == 0);
}

static void
hook_up_pointers (SFileInput *file)
{
    int i;

#if 0
    g_print ("emfle\n");
#endif
    for (i = 0; i < file->n_instructions; ++i)
    {
        Instruction *instruction = &(file->instructions[i]);

        if (instruction->kind == VALUE &&
            instruction->type == TYPE_POINTER)
        {
            gpointer target_object;
            Instruction *target_instruction;

            target_instruction = instruction->u.pointer.target_instruction;

            if (target_instruction)
                target_object = target_instruction->u.begin.end_instruction->u.end.object;
            else
                target_object = NULL;

#if 0
            g_print ("target object: %p\n", target_object);
#endif
            
            *(instruction->u.pointer.location) = target_object;
        }
    }
}

void
sfile_end_get          (SFileInput  *file,
                        const char  *name,
                        gpointer     object)
{
    Instruction *instruction = file->current_instruction++;
    
    g_return_if_fail (instruction->kind == END);
    g_return_if_fail (strcmp (instruction->name, name) == 0);

    instruction->u.end.object = object;

    if (file->current_instruction == file->instructions + file->n_instructions)
        hook_up_pointers (file);
}

static int
get_id (const char **names, const char **values, GError **err)
{
    const char *id_string = NULL;
    int id, i;
    
    for (i = 0; names[i] != NULL; ++i)
    {
	if (strcmp (names[i], "id") != 0)
	{
            set_unknown_attribute_error (err, "Unknown attribute: %s", names[i]);
	    return -1;
	}
        
        if (id_string)
        {
            set_invalid_content_error (err, "Attribute 'id' defined twice");
            return -1;
        }
        
        id_string = values[i];
    }
    
    if (!id_string)
        return 0;
    
    if (!get_number (id_string, &id) || id < 1)
    {
        set_invalid_content_error (err, "Bad attribute value for attribute 'id' (must be >= 1)\n");
        return -1;
    }
    
    return id;
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
    Instruction instruction;

    instruction.u.begin.id = get_id (attribute_names, attribute_values, err);

    if (instruction.u.begin.id == -1)
        return;

    build->state = state_transition_begin (build->state, element_name, &instruction.type);
    if (!build->state)
    {
        set_unknown_element_error (err, "<%s> unexpected here", element_name);
        return;
    }

    /* FIXME - not10: is there really a reason to add begin/end instructions for values? */
    instruction.name = g_strdup (element_name);
    instruction.kind = BEGIN;
    g_array_append_val (build->instructions, instruction);
}

static void
handle_end_element (GMarkupParseContext *context,
		    const gchar *element_name,
		    gpointer user_data,
		    GError **err)
{
    BuildContext *build = user_data;
    Instruction instruction;
    
    build->state = state_transition_end (build->state, element_name, &instruction.type);
    if (!build->state)
    {
        set_unknown_element_error (err, "</%s> unexpected here", element_name);
        return;
    }

    instruction.name = g_strdup (element_name);
    instruction.kind = END;

    g_array_append_val (build->instructions, instruction);
}

static gboolean
decode_text (const char *text, char **decoded)
{
    int length = strlen (text);

    if (length < 2)
        return FALSE;

    if (text[0] != '\"' || text[length - 1] != '\"')
        return FALSE;

    if (decoded)
        *decoded = g_strndup (text + 1, length - 2);
    
    return TRUE;
}

static void
handle_text (GMarkupParseContext *context,
	     const gchar *text,
             gsize text_len,
	     gpointer user_data,
	     GError **err)
{
    BuildContext *build = user_data;
    Instruction instruction;
    char *free_me;
    SType target_type;

    text = free_me = g_strstrip (g_strdup (text));

    if (strlen (text) == 0)
        goto out;
        
    build->state = state_transition_text (build->state, &instruction.type, &target_type);
    if (!build->state)
    {
        int line, ch;
        g_markup_parse_context_get_position (context, &line, &ch);
#if 0
        g_print ("line: %d char: %d\n", line, ch);
#endif
        set_invalid_content_error (err, "Unexpected text data");
        goto out;
    }
        
    instruction.name = NULL;
    instruction.kind = VALUE;
    
    switch (instruction.type)
    {
    case TYPE_POINTER:
	instruction.u.pointer.target_type = target_type;
        if (!get_number (text, &instruction.u.pointer.target_id))
        {
            set_invalid_content_error (err, "Contents '%s' of pointer element is not a number", text);
            goto out;
        }
        break;
        
    case TYPE_INTEGER:
        if (!get_number (text, &instruction.u.integer.value))
        {
            set_invalid_content_error (err, "Contents '%s' of integer element not a number", text);
            goto out;
        }
        break;
        
    case TYPE_STRING:
        if (!decode_text (text, &instruction.u.string.value))
        {
            set_invalid_content_error (err, "Contents '%s' of text element is illformed", text);
            goto out;
        }
        break;
        
    default:
        g_assert_not_reached();
        break;
    }
    
    g_array_append_val (build->instructions, instruction);

 out:
    g_free (free_me);
}

static void
free_instructions (Instruction *instructions, int n_instructions)
{
    int i;

    for (i = 0; i < n_instructions; ++i)
    {
        Instruction *instruction = &(instructions[i]);
        
        if (instruction->name)
            g_free (instruction->name);

        if (instruction->kind == VALUE && instruction->type == TYPE_STRING)
            g_free (instruction->u.string.value);
    }
    
    g_free (instructions);
}

/* This functions makes end instructions point to the corresponding
 * begin instructions, and counts the number of instructions
 * contained in a begin/end pair
 */
static Instruction *
process_instruction_pairs (Instruction *first)
{
    Instruction *instruction;
    int n_elements;

    g_assert (first->kind == BEGIN);

    instruction = first + 1;

    n_elements = 0;
    while (instruction->kind != END)
    {
        if (instruction->kind == BEGIN)
        {
            instruction = process_instruction_pairs (instruction);
            if (!instruction)
                return NULL;
        }
        else
        {
            instruction++;
        }

        n_elements++;
    }

    first->u.begin.n_elements = n_elements;
    first->u.begin.end_instruction = instruction;

    instruction->u.end.begin_instruction = first;

    return instruction + 1;
}
      
static gboolean
post_process_read_instructions (Instruction *instructions, int n_instructions, GError **err)
{
    gboolean retval = TRUE;
    GHashTable *instructions_by_id;
    int i;

    /* count list instructions, check pointers */
    process_instruction_pairs (instructions);
    
    /* Build id->instruction map */
    instructions_by_id = g_hash_table_new (g_direct_hash, g_direct_equal);
    for (i = 0; i < n_instructions; ++i)
    {
        Instruction *instruction = &(instructions[i]);

        if (instruction->kind == BEGIN)
        {
            int id = instruction->u.begin.id;

            if (id)
                g_hash_table_insert (instructions_by_id, GINT_TO_POINTER (id), instruction);
        }
    }

    /* Make pointer instructions point to the corresponding element */
    for (i = 0; i < n_instructions; ++i)
    {
        Instruction *instruction = &(instructions[i]);

        if (instruction->kind == VALUE &&
            instruction->type == TYPE_POINTER)
        {
            int target_id = instruction->u.pointer.target_id;

            if (target_id)
            {
                Instruction *target = g_hash_table_lookup (instructions_by_id,
                                                           GINT_TO_POINTER (target_id));
                
                if (target)
                {
		    if (instruction->u.pointer.target_type == target->type)
		    {
			instruction->u.pointer.target_instruction = target;
		    }
		    else
		    {
			set_invalid_content_error (err, "Id %d references an element of the wrong type",
						   instruction->u.pointer.target_id);
			retval = FALSE;
			break;
		    }
                }
                else
                {
                    set_invalid_content_error (err, "Id %d doesn't reference any record or list",
                                               instruction->u.pointer.target_id);
                    retval = FALSE;
                    break;
                }
            }
            else
            {
                instruction->u.pointer.target_instruction = NULL;
            }
        }
    }
    
    g_hash_table_destroy (instructions_by_id);

    return retval;
}

static Instruction *
build_instructions (const char *contents, SFormat *format, int *n_instructions, GError **err)
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
    
    build.state = sformat_get_start_state (format);
    build.instructions = g_array_new (TRUE, TRUE, sizeof (Instruction));
    
    parse_context = g_markup_parse_context_new (&parser, 0, &build, NULL);
    
    if (!g_markup_parse_context_parse (parse_context, contents, -1, err))
    {
        free_instructions ((Instruction *)build.instructions->data, build.instructions->len);
	return NULL;
    }

    if (!sformat_is_end_state (format, build.state))
    {
        set_invalid_content_error (err, "Premature end of file\n");

        free_instructions ((Instruction *)build.instructions->data, build.instructions->len);
        return NULL;
    }

    if (!post_process_read_instructions ((Instruction *)build.instructions->data,
                                         build.instructions->len, err))
    {
        free_instructions ((Instruction *)build.instructions->data, build.instructions->len);
        return NULL;
    }
        
    *n_instructions = build.instructions->len;
    return (Instruction *)g_array_free (build.instructions, FALSE);
}

SFileInput *
sfile_load (const char  *filename,
	    SFormat       *format,
	    GError     **err)
{
    gchar *contents;
    gsize length;
    SFileInput *input;
    
    if (!g_file_get_contents (filename, &contents, &length, err))
	return NULL;
    
    input = g_new (SFileInput, 1);
    
    input->instructions = build_instructions (contents, format, &input->n_instructions, err);
    
    if (!input->instructions)
    {
        g_free (input);
        g_free (contents);
        return NULL;
    }
    
    g_free (contents);

    input->current_instruction = input->instructions;
    input->instructions_by_location = g_hash_table_new (g_direct_hash, g_direct_equal);
    
    return input;
}

/* Writing */
struct SFileOutput
{
    SFormat *format;
    GArray *instructions;
    GHashTable *objects;
    const State *state;
};

SFileOutput *
sfile_output_new (SFormat       *format)
{
    SFileOutput *output = g_new (SFileOutput, 1);
    
    output->format = format;
    output->instructions = g_array_new (TRUE, TRUE, sizeof (Instruction));
    output->state = sformat_get_start_state (format);
    output->objects = g_hash_table_new (g_direct_hash, g_direct_equal);

    return output;
}

void
sfile_begin_add_record (SFileOutput       *file,
                        const char        *name)
{
    Instruction instruction;
    
    file->state = state_transition_begin (file->state, name, &instruction.type);

    g_return_if_fail (file->state);
    g_return_if_fail (is_record_type (instruction.type));
    
    instruction.kind = BEGIN;    
    instruction.name = g_strdup (name);

    g_array_append_val (file->instructions, instruction);
}

void
sfile_begin_add_list   (SFileOutput *file,
                        const char  *name)
{
    Instruction instruction;

    file->state = state_transition_begin (file->state, name, &instruction.type);

    g_return_if_fail (file->state);
    g_return_if_fail (is_list_type (instruction.type));

    instruction.kind = BEGIN;
    instruction.name = g_strdup (name);

    g_array_append_val (file->instructions, instruction);
}

void
sfile_end_add (SFileOutput       *file,
               const char        *name,
               gpointer           object)
{
    Instruction instruction;

    if (object && g_hash_table_lookup (file->objects, object))
    {
        g_warning ("Adding the same object (%p) twice", object);
        return;
    }
    
    file->state = state_transition_end (file->state, name, &instruction.type);

    if (!file->state)
    {
        g_warning ("invalid call of sfile_end_add()");
        return;
    }

    instruction.kind = END;
    instruction.name = g_strdup (name);
    instruction.u.end.object = object;

    g_array_append_val (file->instructions, instruction);

    if (object)
        g_hash_table_insert (file->objects, object, object);
}

static void
sfile_check_value (SFileOutput *file,
                   const char *name,
                   SType type)
{
    SType tmp_type;
    
    file->state = state_transition_begin (file->state, name, &tmp_type);
    g_return_if_fail (file->state && tmp_type == type);

    file->state = state_transition_text (file->state, &type, NULL);
    g_return_if_fail (file->state && tmp_type == type);
    
    file->state = state_transition_end (file->state, name, &type);
    g_return_if_fail (file->state && tmp_type == type);
}

void
sfile_add_string       (SFileOutput       *file,
                        const char *name,
                        const char *string)
{
    Instruction instruction;

    g_return_if_fail (g_utf8_validate (string, -1, NULL));
    
    sfile_check_value (file, name, TYPE_STRING);

    instruction.kind = VALUE;
    instruction.type = TYPE_STRING;
    instruction.name = g_strdup (name);
    instruction.u.string.value = g_strdup (string);

    g_array_append_val (file->instructions, instruction);
}

void
sfile_add_integer      (SFileOutput *file,
                        const char  *name,
                        int          integer)
{
    Instruction instruction;
    
    sfile_check_value (file, name, TYPE_INTEGER);

    instruction.kind = VALUE;
    instruction.type = TYPE_INTEGER;
    instruction.name = g_strdup (name);
    instruction.u.integer.value = integer;

    g_array_append_val (file->instructions, instruction);
}

void
sfile_add_pointer      (SFileOutput *file,
                        const char  *name,
                        gpointer     pointer)
{
    Instruction instruction;
    
    sfile_check_value (file, name, TYPE_POINTER);

    instruction.kind = VALUE;
    instruction.type = TYPE_POINTER;
    instruction.name = g_strdup (name);
    instruction.u.pointer.target_object = pointer;

    g_array_append_val (file->instructions, instruction);
}

static void
post_process_write_instructions (SFileOutput *sfile)
{
    int i;
    Instruction *instructions = (Instruction *)sfile->instructions->data;
    int n_instructions = sfile->instructions->len;
    int id;
    GHashTable *instructions_by_object;
    
    process_instruction_pairs (instructions);

    /* Set all id's to -1 and create map from objects to instructions */

    instructions_by_object = g_hash_table_new (g_direct_hash, g_direct_equal);
    
    for (i = 0; i < n_instructions; ++i)
    {
        Instruction *instruction = &(instructions[i]);

        if (instruction->kind == BEGIN)
        {
            instruction->u.begin.id = -1;
        }
        else if (instruction->kind == END && instruction->u.end.object)
        {
            g_hash_table_insert (instructions_by_object,
                                 instruction->u.end.object, instruction);
        }
    }

    /* Assign an id to all pointed-to instructions */
    id = 1;
    for (i = 0; i < n_instructions; ++i)
    {
        Instruction *instruction = &(instructions[i]);

        if (instruction->type == TYPE_POINTER)
        {
            if (instruction->u.pointer.target_object)
            {
                Instruction *target;

                target =
                    g_hash_table_lookup (instructions_by_object,
                                         instruction->u.pointer.target_object);
                
                if (!target)
                {
                    g_warning ("pointer has unknown target\n");
                    return;
                }
                
                g_assert (target->kind == END);
                
                if (target->u.end.begin_instruction->u.begin.id == -1)
                    target->u.end.begin_instruction->u.begin.id = id++;

                instruction->u.pointer.target_id = 
                    target->u.end.begin_instruction->u.begin.id;
            }
            else
            {
                instruction->u.pointer.target_id = 0;
            }
        }
    }
    
}

static void
add_indent (GString *output, int indent)
{
    int i;

    for (i = 0; i < indent; ++i)
        g_string_append_c (output, ' ');
}

static void
add_integer (GString *output, int value)
{
    g_string_append_printf (output, "%d", value);
}

static void
add_string (GString *output, const char *str)
{
    char *escaped = g_markup_escape_text (str, -1);
    g_string_append_c (output, '\"');
    g_string_append (output, escaped);
    g_string_append_c (output, '\"');
    g_free (escaped);
}

static void
add_begin_tag (GString *output, int indent, const char *name, int id)
{
    add_indent (output, indent);

    if (id != -1)
        g_string_append_printf (output, "<%s id=\"%d\">", name, id);
    else
        g_string_append_printf (output, "<%s>", name);
}

static void
add_end_tag (GString *output, int indent, const char *name)
{
    add_indent (output, indent);
    g_string_append_printf (output, "</%s>", name);
}

static void
add_nl (GString *output)
{
    g_string_append_c (output, '\n');
}

static gboolean
file_replace (const gchar *filename,
              const gchar *contents,
              gssize	     length,
              GError	   **error);

#if 0
static void
disaster (int status)
{
    const char *error;
    switch (status)
    {
    case BZ_PARAM_ERROR:
	error = "BZ_PARAM_ERROR";
	break;
	
    case BZ_MEM_ERROR:
	error = "BZ_MEM_ERROR";
	break;
	
    case BZ_OUTBUFF_FULL:
	error = "BZ_OUTBUFF_FULL";
	break;

    default:
	error = "Unknown error";
	break;
    }
    
    g_error ("Failed to compress file: %s\n", error);
}

static void
bz2_compress (const guchar *input, int input_length,
	      guchar **output, int *output_length)
{
    size_t compressed_size;
    guchar *compressed_data;
    int status;
    
    g_return_if_fail (input != NULL);
    
    /* The bzip2 manual says:
     *
     * To guarantee that the compressed data will fit in its buffer,
     * allocate an output buffer of size 1% larger than the uncompressed
     * data, plus six hundred extra bytes.
     */
    compressed_size = (size_t)(1.02 * input_length + 600);
    compressed_data = g_malloc (compressed_size);
    
    status = BZ2_bzBuffToBuffCompress (compressed_data, &compressed_size,
                                       (guchar *)input, input_length,
                                       9 /* block size */,
                                       0 /* verbosity */,
                                       0 /* workfactor */);
    
    if (status != BZ_OK)
	disaster (status);
    
    if (output)
	*output = compressed_data;
    else
	g_free (compressed_data);

    if (output_length)
	*output_length = compressed_size;
}
#endif

gboolean
sfile_output_save (SFileOutput  *sfile,
                   const char   *filename,
                   GError      **err)
{
    int i;
    Instruction *instructions;
    GString *output;
    int indent;
    gboolean retval;
#if 0
    guchar *compressed;
    size_t compressed_size;
#endif

    g_return_val_if_fail (sfile != NULL, FALSE);

    instructions = (Instruction *)sfile->instructions->data;

    post_process_write_instructions (sfile);
    
    indent = 0;
    output = g_string_new ("");
    for (i = 0; i < sfile->instructions->len; ++i)
    {
        Instruction *instruction = &(instructions[i]);

        switch (instruction->kind)
        {
        case BEGIN:
            add_begin_tag (output, indent, instruction->name,
                           instruction->u.begin.id);
            add_nl (output);
            indent += 4;
            break;
            
        case END:
            indent -= 4;
            add_end_tag (output, indent, instruction->name);
            add_nl (output);
            break;

        case VALUE:
            add_begin_tag (output, indent, instruction->name, -1);
            switch (instruction->type)
            {
            case TYPE_INTEGER:
                add_integer (output, instruction->u.integer.value);
                break;

            case TYPE_POINTER:
                add_integer (output, instruction->u.pointer.target_id);
                break;

            case TYPE_STRING:
                add_string (output, instruction->u.string.value);
                break;
            }
            add_end_tag (output, 0, instruction->name);
            add_nl (output);
            break;
        }
    }

#if 0
    /* FIXME - not10: bz2 compressing the output is probably
     * interesting at some point. For now just make sure
     * it works without actually using it.
     */
    bz2_compress (output->str, output->len,
                  &compressed, &compressed_size);

    g_free (compressed);
#endif
    
    retval = file_replace (filename, output->str, - 1, err);
    
    g_string_free (output, TRUE);

    return retval;
}


void
sfile_input_free	(SFileInput  *file)
{
    free_instructions (file->instructions, file->n_instructions);

    g_hash_table_destroy (file->instructions_by_location);

    g_free (file);
}

void
sfile_output_free (SFileOutput *sfile)
{
    Instruction *instructions;
    int n_instructions;

    n_instructions = sfile->instructions->len;
    instructions = (Instruction *)g_array_free (sfile->instructions, FALSE);

    free_instructions (instructions, n_instructions);

    g_hash_table_destroy (sfile->objects);
    g_free (sfile);
}













/* A copy of g_file_replace() because I don't want to depend on
 * GLib HEAD
 */
#include <errno.h>
#include <sys/wait.h>
#include <glib/gstdio.h>
#include <unistd.h>

static gboolean
rename_file (const char *old_name,
	     const char *new_name,
	     GError **err)
{
  errno = 0;
  if (g_rename (old_name, new_name) == -1)
    {
      int save_errno = errno;
      gchar *display_old_name = g_filename_display_name (old_name);
      gchar *display_new_name = g_filename_display_name (new_name);

      g_set_error (err,
		   G_FILE_ERROR,
		   g_file_error_from_errno (save_errno),
		   "Failed to rename file '%s' to '%s': g_rename() failed: %s",
		   display_old_name,
		   display_new_name,
		   g_strerror (save_errno));

      g_free (display_old_name);
      g_free (display_new_name);
      
      return FALSE;
    }
  
  return TRUE;
}

static gboolean
set_umask_permissions (int	     fd,
		       GError      **err)
{
#ifdef G_OS_WIN32

  return TRUE;

#else

  /* All of this function is just to work around the fact that
   * there is no way to get the umask without changing it.
   *
   * We can't just change-and-reset the umask because that would
   * lead to a race condition if another thread tried to change
   * the umask in between the getting and the setting of the umask.
   * So we have to do the whole thing in a child process.
   */
  
  pid_t pid = fork ();

  if (pid == -1)
    {
      g_set_error (err,
		   G_FILE_ERROR,
		   g_file_error_from_errno (errno),
		   "Could not change file mode: fork() failed: %s",
		   g_strerror (errno));
      
      return FALSE;
    }
  else if (pid == 0)
    {
      /* child */
      mode_t mask = umask (0666);

      errno = 0;
      if (fchmod (fd, 0666 & ~mask) == -1)
	_exit (errno);
      else
	_exit (0);

      return TRUE; /* To quiet gcc */
    }
  else
    { 
      /* parent */
      int status;
      
      waitpid (pid, &status, 0);

      if (WIFEXITED (status))
	{
	  int chmod_errno = WEXITSTATUS (status);

	  if (chmod_errno == 0)
	    {
	      return TRUE;
	    }
	  else
	    {
	      g_set_error (err,
			   G_FILE_ERROR,
			   g_file_error_from_errno (chmod_errno),
			   "Could not change file mode: chmod() failed: %s",
			   g_strerror (chmod_errno));
      
	      return FALSE;
	    }
	}
      else if (WIFSIGNALED (status))
	{
	  g_set_error (err,
		       G_FILE_ERROR,
		       G_FILE_ERROR_FAILED,
		       "Could not change file mode: Child terminated by signal: %s",
		       g_strsignal (WTERMSIG (status)));
		       
	  return FALSE;
	}
      else
	{
	  /* This shouldn't happen */
	  g_set_error (err,
		       G_FILE_ERROR,
		       G_FILE_ERROR_FAILED,
		       "Could not change file mode: Child terminated abnormally");
	  return FALSE;
	}
    }
#endif
}

static gchar *
write_to_temp_file (const gchar *contents,
		    gssize length,
		    const gchar *template,
		    GError **err)
{
  gchar *tmp_name;
  gchar *display_name;
  gchar *retval;
  FILE *file;
  gint fd;
  int save_errno;

  retval = NULL;
  
  tmp_name = g_strdup_printf ("%s.XXXXXX", template);

  errno = 0;
  fd = g_mkstemp (tmp_name);
  save_errno = errno;
  display_name = g_filename_display_name (tmp_name);
      
  if (fd == -1)
    {
      g_set_error (err,
		   G_FILE_ERROR,
		   g_file_error_from_errno (save_errno),
		   "Failed to create file '%s': %s",
		   display_name, g_strerror (save_errno));
      
      goto out;
    }

  if (!set_umask_permissions (fd, err))
    {
      close (fd);
      g_unlink (tmp_name);

      goto out;
    }
  
  errno = 0;
  file = fdopen (fd, "wb");
  if (!file)
    {
      g_set_error (err,
		   G_FILE_ERROR,
		   g_file_error_from_errno (errno),
		   "Failed to open file '%s' for writing: fdopen() failed: %s",
		   display_name,
		   g_strerror (errno));

      close (fd);
      g_unlink (tmp_name);
      
      goto out;
    }

  if (length > 0)
    {
      size_t n_written;
      
      errno = 0;

      n_written = fwrite (contents, 1, length, file);
      
      if (n_written < length)
	{
 	  g_set_error (err,
		       G_FILE_ERROR,
		       g_file_error_from_errno (errno),
		       "Failed to write file '%s': fwrite() failed: %s",
		       display_name,
		       g_strerror (errno));

	  fclose (file);
	  g_unlink (tmp_name);
	  
	  goto out;
	}
    }
   
  errno = 0;
  if (fclose (file) == EOF)
    {
      g_set_error (err,
		   G_FILE_ERROR,
		   g_file_error_from_errno (errno),
		   "Failed to close file '%s': fclose() failed: %s",
		   display_name, 
		   g_strerror (errno));

      g_unlink (tmp_name);
      
      goto out;
    }

  retval = g_strdup (tmp_name);
  
 out:
  g_free (tmp_name);
  g_free (display_name);
  
  return retval;
}

static gboolean
file_replace (const gchar *filename,
              const gchar *contents,
              gssize	     length,
              GError	   **error)
{
  gchar *tmp_filename;
  gboolean retval;
  GError *rename_error = NULL;
  
  g_return_val_if_fail (filename != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  g_return_val_if_fail (contents != NULL || length == 0, FALSE);
  g_return_val_if_fail (length >= -1, FALSE);
  
  if (length == -1)
    length = strlen (contents);

  tmp_filename = write_to_temp_file (contents, length, filename, error);
  
  if (!tmp_filename)
    {
      retval = FALSE;
      goto out;
    }

  if (!rename_file (tmp_filename, filename, &rename_error))
    {
#ifndef G_OS_WIN32

      g_unlink (tmp_filename);
      g_propagate_error (error, rename_error);
      retval = FALSE;
      goto out;

#else /* G_OS_WIN32 */
      
      /* Renaming failed, but on Windows this may just mean
       * the file already exists. So if the target file
       * exists, try deleting it and do the rename again.
       */
      if (!g_file_test (filename, G_FILE_TEST_EXISTS))
	{
	  g_unlink (tmp_filename);
	  g_propagate_error (error, rename_error);
	  retval = FALSE;
	  goto out;
	}

      g_error_free (rename_error);
      
      if (g_unlink (filename) == -1)
	{
          gchar *display_filename = g_filename_display_name (filename);

	  g_set_error (error,
		       G_FILE_ERROR,
		       g_file_error_from_errno (errno),
		       "Existing file '%s' could not be removed: g_unlink() failed: %s",
		       display_filename,
		       g_strerror (errno));

	  g_free (display_filename);
	  g_unlink (tmp_filename);
	  retval = FALSE;
	  goto out;
	}
      
      if (!rename_file (tmp_filename, filename, error))
	{
	  g_unlink (tmp_filename);
	  retval = FALSE;
	  goto out;
	}

#endif
    }

  retval = TRUE;
  
 out:
  g_free (tmp_filename);
  return retval;
}

