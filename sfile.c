/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*- */

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
#define FIRST_BEGIN_TRANSITION BEGIN_RECORD
    BEGIN_RECORD,
    BEGIN_LIST,
    BEGIN_POINTER,
    BEGIN_INTEGER,
    BEGIN_STRING,
#define LAST_BEGIN_TRANSITION BEGIN_STRING
    
#define FIRST_END_TRANSITION END_RECORD
    END_RECORD,
    END_LIST,
    END_POINTER,
    END_INTEGER,
    END_STRING,
#define LAST_END_TRANSITION END_STRING
    
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
    TransitionType type;
    
    char *name;
    union
    {
        struct
        {
        } begin_record;
        
        struct
        {
            int n_items;
        } begin_list;
        
        struct
        {
            int id;
        } end;
        
        struct
        {
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

static State *
state_new (void)
{
    State *state = g_new (State, 1);
    state->transitions = g_queue_new ();
    return state;
}

static Transition *
transition_new (const char *element, TransitionType type, State *from, State *to)
{
    Transition *t = g_new (Transition, 1);
    
    t->element = element? g_strdup (element) : NULL;
    t->type = type;
    t->to = to;
    
    if (from)
        g_queue_push_tail (from->transitions, t);
    
    return t;
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
    enter = transition_new (name, BEGIN_RECORD, NULL, begin);
    
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
    fragment->exit = transition_new (name, END_RECORD, state, NULL);
    
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
    
    enter = transition_new (name, BEGIN_LIST, NULL, list_state);
    exit = transition_new (name, END_LIST, list_state, NULL);
    
    g_queue_push_tail (list_state->transitions, m->enter);
    m->exit->to = list_state;
    
    m->enter = enter;
    m->exit = exit;
    
    return m;
}

static gpointer
sformat_new_value (const char *name, TransitionType type)
{
    Fragment *m = g_new (Fragment, 1);
    State *before, *after;
    Transition *value;
    
    before = state_new ();
    after = state_new ();
    
    m->enter = transition_new (name, type, NULL, before);
    m->exit  = transition_new (name, type, after, NULL);
    value = transition_new (NULL, type, before, after);
    
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
state_transition_check (const State *state, const char *element,
                        TransitionType first, TransitionType last,
                        TransitionType *type, GError **err)
{
    GList *list;
    
    for (list = state->transitions->head; list; list = list->next)
    {
        Transition *transition;
        
        if (transition->type >= first                   &&
            transition->type <= last                    &&
            strcmp (element, transition->element) == 0)
        {
            *type = transition->type;
            return transition->to;
        }
    }
    
    set_unknown_element_error (err, "<%s> or </%s> unexpected here", element, element);
    
    return NULL;
}

static const State *
state_transition_begin (const State *state, const char *element,
                        TransitionType *type, GError **err)
{
    return state_transition_check (state, element,
                                   FIRST_BEGIN_TRANSITION, LAST_BEGIN_TRANSITION,
                                   type, err);
}

static const State *
state_transition_end (const State *state, const char *element,
                      TransitionType *type, GError **err)
{
    return state_transition_check (state, element,
                                   FIRST_END_TRANSITION, LAST_END_TRANSITION,
                                   type, err);
}

static const State *
state_transition_text (const State *state, TransitionType *type, GError **err)
{
    GList *list;
    
    for (list = state->transitions->head; list; list = list->next)
    {
        Transition *transition = list->data;
        
        /* There will never be more than one allowed value transition for
         * a given state
         */
        if (transition->type == POINTER ||
            transition->type == INTEGER ||
            transition->type == STRING)
        {
            *type = transition->type;
            return transition->to;
        }
    }
    
    set_invalid_content_error (err, "Unexpected text data");
    return NULL;
}

/* reading */
typedef struct BuildContext BuildContext;
typedef struct ParseNode ParseNode;

struct BuildContext
{
    const State *state;
    
    GArray *actions;
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

struct SFileInput
{
    int n_actions;
    Action *actions;
    Action *current_action;
    GHashTable *actions_by_location;
    GHashTable *objects_by_id;
};

void
sfile_begin_get_record (SFileInput *file, const char *name)
{
    Action *action = file->current_action++;
    
    g_return_if_fail (action->type == BEGIN_RECORD &&
                      strcmp (action->name, name) == 0);
}

int
sfile_begin_get_list   (SFileInput       *file,
                        const char   *name)
{
    Action *action = file->current_action++;
    
    g_return_val_if_fail (action->type == BEGIN_LIST &&
                          strcmp (action->name, name) == 0, 0);
    
    return action->u.begin_list.n_items;
}

void
sfile_get_pointer      (SFileInput  *file,
                        const char *name,
                        gpointer    *location)
{
    Action *action = file->current_action++;
    g_return_if_fail (action->type == POINTER &&
                      strcmp (action->name, name) == 0);
    
    action->u.pointer.location = location;
    
    if (location)
    {
        if (g_hash_table_lookup (file->actions_by_location, location))
            g_warning ("Reading into the same location twice\n");
        
        g_hash_table_insert (file->actions_by_location, location, action);
    }
}

void
sfile_get_integer      (SFileInput  *file,
                        const char  *name,
                        int         *integer)
{
    Action *action = file->current_action++;
    g_return_if_fail (action->type == INTEGER &&
                      strcmp (action->name, name) == 0);
    
    if (integer)
        *integer = action->u.integer.value;
}

void
sfile_get_string       (SFileInput  *file,
                        const char  *name,
                        char       **string)
{
    Action *action = file->current_action++;
    g_return_if_fail (action->type == STRING &&
                      strcmp (action->name, name) == 0);
    
    if (string)
        *string = g_strdup (action->u.string.value);
}

void
sfile_end_get          (SFileInput  *file,
                        const char  *name,
                        gpointer     object)
{
    Action *action = file->current_action++;
    
    g_return_if_fail ((action->type == END_LIST ||
                       action->type == END_RECORD) &&
                      strcmp (action->name, name) == 0);
    
    if (action->u.end.id)
        g_hash_table_insert (file->objects_by_id,
                             GINT_TO_POINTER (action->u.end.id), object);
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
    Action action;
    int id;
    
    /* Check for id */
    id = get_id (attribute_names, attribute_values, err);
    
    if (id == -1)
        return;
    
    build->state = state_transition_begin (build->state, element_name, &action.type, err);
    
    /* FIXME create action/add to list */
    /* FIXME figure out how to best count the number of items if action.type is a list */
}

static void
handle_end_element (GMarkupParseContext *context,
		    const gchar *element_name,
		    gpointer user_data,
		    GError **err)
{
    BuildContext *build = user_data;
    Action action;
    
    build->state = state_transition_end (build->state, element_name, &action.type, err);
    
    /* FIXME create action/add to list */
}

static void
handle_text (GMarkupParseContext *context,
	     const gchar *text,
             gsize text_len,
	     gpointer user_data,
	     GError **err)
{
    BuildContext *build = user_data;
    Action action;
    
    build->state = state_transition_text (build->state, &action.type, err);
    
    /* FIXME create acxtion/add to list */
    /* FIXME check that the text makes sense according to action.type */
}

static void
free_actions (Action *actions, int n_actions)
{
    int i;
    
    for (i = 0; i < n_actions; ++i)
    {
        Action *action = &(actions[i]);
        
        if (action->name)
            g_free (action->name);
        
        if (action->type == STRING)
            g_free (action->u.string.value);
    }
    
    g_free (actions);
}

static Action *
build_actions (const char *contents, SFormat *format, int *n_actions, GError **err)
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
    parse_context = g_markup_parse_context_new (&parser, 0, &build, NULL);
    if (!sformat_is_end_state (format, build.state))
    {
        set_invalid_content_error (err, "Unexpected end of file\n");
        
        free_actions ((Action *)build.actions->data, build.actions->len);
        return NULL;
    }
    
    if (!g_markup_parse_context_parse (parse_context, contents, -1, err))
    {
        free_actions ((Action *)build.actions->data, build.actions->len);
	return NULL;
    }
    
    *n_actions = build.actions->len;
    return (Action *)g_array_free (build.actions, FALSE);
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
    
    input->actions = build_actions (contents, format, &input->n_actions, err);
    
    if (!input->actions)
    {
        g_free (input);
        g_free (contents);
        return NULL;
    }
    
    g_free (contents);
    
    return input;
}

/* Writing */

struct SFileOutput
{
    SFormat *format;
    const State *state;
};


SFileOutput *
sfile_output_mew (SFormat       *format)
{
    SFileOutput *output = g_new (SFileOutput, 1);
    
    output->format = format;
    output->state = sformat_get_start_state (format);

    return output;
}

void
sfile_begin_add_record (SFileOutput       *file,
                        const char *name,
                        gpointer     id)
{
    TransitionType type;
    
    file->state = state_transition_begin (file->state, name, &type, NULL);

    g_return_if_fail (file->state && type == BEGIN_RECORD);
    
    /* FIXME: add action including id */
}

void
sfile_begin_add_list   (SFileOutput       *file,
                        const char *name,
                        gpointer     id)
{
    TransitionType type;

    file->state = state_transition_begin (file->state, name, &type, NULL);

    g_return_if_fail (file->state && type == BEGIN_LIST);

    /* FIXME */
}

void
sfile_end_add          (SFileOutput       *file,
                        const char *name)
{
    TransitionType type;

    file->state = state_transition_end (file->state, name, &type, NULL);

    g_return_if_fail (file->state && (type == END_RECORD || type == END_LIST));

    /* FIXME */
}

void
sfile_add_string       (SFileOutput       *file,
                        const char *name,
                        const char  *string)
{
    TransitionType type;

    file->state = state_transition_begin (file->state, name, &type, NULL);

    g_return_if_fail (file->state && type == BEGIN_STRING);

    file->state = state_transition_text (file->state, &type, NULL);

    g_return_if_fail (file->state && type == STRING);

    file->state = state_transition_end (file->state, name, &type, NULL);

    g_return_if_fail (file->state && type == END_STRING);
}

void
sfile_add_integer      (SFileOutput       *file,
                        const        char *name,
                        int          integer)
{
    TransitionType type;

    /* FIXME: check intermediate states */
    
    file->state = state_transition_begin (file->state, name, &type, NULL);

    file->state = state_transition_text (file->state, &type, NULL);

    file->state = state_transition_end (file->state, name, &type, NULL);
}

void
sfile_add_pointer      (SFileOutput       *file,
                        const char *name,
                        gpointer     pointer)
{
    /* FIMXE */
}

gboolean
sfile_save             (SFileOutput       *sfile,
                        const char  *filename,
                        GError     **err)
{
    return FALSE; /* FIXME */
}
