/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*- */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include "sfile.h"

typedef struct State State;
typedef struct Transition Transition;
typedef struct Fragment Fragment;

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

#define FIRST_VALUE_TRANSITION POINTER
    POINTER,
    INTEGER,
    STRING
#define LAST_VALUE_TRANSITION STRING
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
 *
 *
 * Also consider adding the following data types:
 *
 * Binary blobs of data, stored as base64 perhaps
 * floating point values. How do we store those portably
 *     without losing precision?
 * enums, stored as strings
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
    State *begin, *state;
    Fragment *fragment;
    GList *list;
    
    /* Build queue of fragments */
    va_start (args, content1);
    
    fragments = fragment_queue (args);
    
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
    
    /* Return resulting fragment */
    fragment = g_new (Fragment, 1);
    fragment->enter = transition_new (name, BEGIN_RECORD, NULL, begin);
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
sformat_new_value (const char *name,
                   TransitionType enter,
                   TransitionType type,
                   TransitionType exit)
{
    Fragment *m = g_new (Fragment, 1);
    State *before, *after;
    Transition *value;
    
    before = state_new ();
    after = state_new ();
    
    m->enter = transition_new (name, enter, NULL, before);
    m->exit  = transition_new (name, type, after, NULL);
    value = transition_new (NULL, exit, before, after);
    
    return m;
}

gpointer
sformat_new_pointer (const char *name)
{
    return sformat_new_value (name, BEGIN_POINTER, POINTER, END_POINTER);
}

gpointer
sformat_new_integer (const char *name)
{
    return sformat_new_value (name, BEGIN_INTEGER, INTEGER, END_INTEGER);
}

gpointer
sformat_new_string (const char *name)
{
    return sformat_new_value (name, BEGIN_STRING, STRING, END_STRING);
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
        
        if (transition->type == POINTER ||
            transition->type == INTEGER ||
            transition->type == STRING)
        {
            *type = transition->type;

            /* There will never be more than one allowed value transition for
             * a given state
             */
            return transition->to;
        }
    }
    
    set_invalid_content_error (err, "Unexpected text data");
    return NULL;
}

/* reading */
typedef struct BuildContext BuildContext;
typedef struct ReadItem ReadItem;

struct ReadItem
{
    TransitionType type;
    
    char *name;
    union
    {
        struct
        {
            int n_items;
            int id;
            ReadItem *end_item;
        } begin;
        
        struct
        {
            gpointer object;
        } end;
        
        struct
        {
            int target_id;
            ReadItem *target_item;
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

    GArray *items;
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
    int n_items;
    ReadItem *items;
    ReadItem *current_item;
    GHashTable *items_by_location;
    GHashTable *objects_by_id;
};

void
sfile_begin_get_record (SFileInput *file, const char *name)
{
    ReadItem *item = file->current_item++;
    
    g_return_if_fail (item->type == BEGIN_RECORD &&
                      strcmp (item->name, name) == 0);
}

int
sfile_begin_get_list   (SFileInput       *file,
                        const char   *name)
{
    ReadItem *item = file->current_item++;
    
    g_return_val_if_fail (item->type == BEGIN_LIST &&
                          strcmp (item->name, name) == 0, 0);
    
    return item->u.begin.n_items;
}

void
sfile_get_pointer (SFileInput  *file,
                   const char *name,
                   gpointer    *location)
{
    ReadItem *item = file->current_item++;
    g_return_if_fail (item->type == POINTER &&
                      strcmp (item->name, name) == 0);
    
    item->u.pointer.location = location;

    *location = (gpointer) 0xFedeAbe;

    if (location)
    {
        if (g_hash_table_lookup (file->items_by_location, location))
            g_warning ("Reading into the same location twice\n");
        
        g_hash_table_insert (file->items_by_location, location, item);
    }
}

void
sfile_get_integer      (SFileInput  *file,
                        const char  *name,
                        int         *integer)
{
    ReadItem *item = file->current_item++;
    g_return_if_fail (item->type == INTEGER &&
                      strcmp (item->name, name) == 0);
    
    if (integer)
        *integer = item->u.integer.value;
}

void
sfile_get_string       (SFileInput  *file,
                        const char  *name,
                        char       **string)
{
    ReadItem *item = file->current_item++;
    g_return_if_fail (item->type == STRING &&
                      strcmp (item->name, name) == 0);
    
    if (string)
        *string = g_strdup (item->u.string.value);
}

static void
hook_up_pointers (SFileInput *file)
{
    int i;

    for (i = 0; i < file->n_items; ++i)
    {
        ReadItem *item = &(file->items[i]);

        if (item->type == POINTER)
        {
            gpointer target_object =
                item->u.pointer.target_item->u.begin.end_item->u.end.object;

            *(item->u.pointer.location) = target_object;
        }
    }
}

void
sfile_end_get          (SFileInput  *file,
                        const char  *name,
                        gpointer     object)
{
    ReadItem *item = file->current_item++;
    
    g_return_if_fail ((item->type == END_LIST ||
                       item->type == END_RECORD) &&
                      strcmp (item->name, name) == 0);

    item->u.end.object = object;

    if (file->current_item == file->items + file->n_items)
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
    ReadItem item;
    
    item.u.begin.id = get_id (attribute_names, attribute_values, err);

    if (item.u.begin.id == -1)
        return;

    build->state = state_transition_begin (build->state, element_name, &item.type, err);
    item.name = g_strdup (element_name);
    
    g_array_append_val (build->items, item);
}

static void
handle_end_element (GMarkupParseContext *context,
		    const gchar *element_name,
		    gpointer user_data,
		    GError **err)
{
    BuildContext *build = user_data;
    ReadItem item;
    
    build->state = state_transition_end (build->state, element_name, &item.type, err);

    item.name = g_strdup (element_name);

    g_array_append_val (build->items, item);
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
    ReadItem item;
    
    build->state = state_transition_text (build->state, &item.type, err);

    item.name = NULL;

    switch (item.type)
    {
    case POINTER:
        if (!get_number (text, &item.u.pointer.target_id))
        {
            set_invalid_content_error (err, "Contents '%s' of pointer element is not a number", text);
            return;
        }
        break;

    case INTEGER:
        if (!get_number (text, &item.u.integer.value))
        {
            set_invalid_content_error (err, "Contents '%s' of integer element not a number", text);
            return;
        }
        break;

    case STRING:
        if (!decode_text (text, &item.u.string.value))
        {
            set_invalid_content_error (err, "Contents '%s' of text element is illformed", text);
            return;
        }
        break;

    default:
        g_assert_not_reached();
        break;
    }

    g_array_append_val (build->items, item);
}

static void
free_items (ReadItem *items, int n_items)
{
    int i;
    
    for (i = 0; i < n_items; ++i)
    {
        ReadItem *item = &(items[i]);
        
        if (item->name)
            g_free (item->name);
        
        if (item->type == STRING)
            g_free (item->u.string.value);
    }
    
    g_free (items);
}

/* This functions counts the number of items in each list, and
 * matches up pointers with the lists/records they point to.
 * FIMXE: think of a better name
 */
static ReadItem *
post_process_items_recurse (ReadItem *first, GHashTable *items_by_id, GError **err)
{
    ReadItem *item;
    int n_items;

    g_assert (first->type >= FIRST_BEGIN_TRANSITION &&
              first->type <= LAST_BEGIN_TRANSITION);
    
    item = first + 1;

    n_items = 0;
    while (item->type < FIRST_END_TRANSITION ||
           item->type > LAST_END_TRANSITION)
    {
        if (item->type >= FIRST_BEGIN_TRANSITION &&
            item->type <= LAST_BEGIN_TRANSITION)
        {
            item = post_process_items_recurse (item, items_by_id, err);
            if (!item)
                return NULL;
        }
        else
        {
            if (item->type == POINTER)
            {
                int target_id = item->u.pointer.target_id;
                ReadItem *target = g_hash_table_lookup (items_by_id, GINT_TO_POINTER (target_id));

                if (!target)
                {
                    set_invalid_content_error (err, "Id %d doesn't reference any record or list\n",
                                               item->u.pointer.target_id);
                    return NULL;
                }

                item->u.pointer.target_item = target;
            }
            
            item++;
        }
        
        n_items++;
    }

    first->u.begin.n_items = n_items;
    first->u.begin.end_item = item;

    return item + 1;
}
        
static gboolean
post_process_items (ReadItem *items, int n_items, GError **err)
{
    gboolean retval = TRUE;
    GHashTable *items_by_id;
    int i;

    /* Build id->item map */
    items_by_id = g_hash_table_new (g_direct_hash, g_direct_equal);
    for (i = 0; i < n_items; ++i)
    {
        ReadItem *item = &(items[i]);

        if (item->type >= FIRST_BEGIN_TRANSITION &&
            item->type <= LAST_BEGIN_TRANSITION)
        {
            int id = item->u.begin.id;

            if (id)
                g_hash_table_insert (items_by_id, GINT_TO_POINTER (id), item);
        }
    }

    /* count list items, check pointers */
    if (!post_process_items_recurse (items, items_by_id, err))
        retval = FALSE;
    
    g_hash_table_destroy (items_by_id);

    return retval;
}

static ReadItem *
build_items (const char *contents, SFormat *format, int *n_items, GError **err)
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
    build.items = g_array_new (TRUE, TRUE, sizeof (ReadItem));
    
    parse_context = g_markup_parse_context_new (&parser, 0, &build, NULL);
    if (!sformat_is_end_state (format, build.state))
    {
        set_invalid_content_error (err, "Unexpected end of file\n");

        free_items ((ReadItem *)build.items->data, build.items->len);
        return NULL;
    }
    
    if (!g_markup_parse_context_parse (parse_context, contents, -1, err))
    {
        free_items ((ReadItem *)build.items->data, build.items->len);
	return NULL;
    }

    if (!post_process_items ((ReadItem *)build.items->data, build.items->len, err))
    {
        free_items ((ReadItem *)build.items->data, build.items->len);
        return NULL;
    }
        
    *n_items = build.items->len;
    return (ReadItem *)g_array_free (build.items, FALSE);
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
    
    input->items = build_items (contents, format, &input->n_items, err);
    
    if (!input->items)
    {
        g_free (input);
        g_free (contents);
        return NULL;
    }
    
    g_free (contents);
    
    return input;
}

/* Writing */
typedef struct WriteItem WriteItem;

struct WriteItem
{
    TransitionType type;
    union
    {
        struct
        {
            char *name;
            int id;
        } begin;

        struct
        {
            char *name;
        } end;

        struct
        {
            char *name;
            gpointer object;
        } pointer;

        struct
        {
            char *name;
            int value;
        } integer;

        struct
        {
            char *name;
            char *value;
        } string;
    } u;
};

struct SFileOutput
{
    SFormat *format;
    GArray *items;
    const State *state;
};


SFileOutput *
sfile_output_mew (SFormat       *format)
{
    SFileOutput *output = g_new (SFileOutput, 1);
    
    output->format = format;
    output->items = g_array_new (TRUE, TRUE, sizeof (WriteItem));
    output->state = sformat_get_start_state (format);

    return output;
}

void
sfile_begin_add_record (SFileOutput       *file,
                        const char        *name)
{
    WriteItem item;
    
    file->state = state_transition_begin (
        file->state, name, &item.type, NULL);

    g_return_if_fail (file->state && item.type == BEGIN_RECORD);

    item.u.begin.name = g_strdup (name);

    g_array_append_val (file->items, item);
}

void
sfile_begin_add_list   (SFileOutput *file,
                        const char  *name)
{
    WriteItem item;
    TransitionType type;

    file->state = state_transition_begin (
        file->state, name, &item.type, NULL);

    g_return_if_fail (file->state && type == BEGIN_LIST);

    item.u.begin.name = g_strdup (name);

    g_array_append_val (file->items, item);
}

void
sfile_end_add          (SFileOutput       *file,
                        const char        *name,
                        gpointer           object)
{
    WriteItem item;

    file->state = state_transition_end (
        file->state, name, &item.type, NULL);

    g_return_if_fail (file->state &&
                      (item.type == END_RECORD || item.type == END_LIST));

    item.u.end.name = g_strdup (name);

    g_array_append_val (file->items, item);
}

static TransitionType
sfile_add_value (SFileOutput *file,
                 const char *name,
                 TransitionType begin,
                 TransitionType value,
                 TransitionType end)
{
    TransitionType tmp_type;
    TransitionType type;
    
    file->state = state_transition_begin (file->state, name, &tmp_type, NULL);
    g_return_if_fail (file->state && tmp_type == begin);

    file->state = state_transition_text (file->state, &type, NULL);
    g_return_if_fail (file->state && type == value);
    
    file->state = state_transition_end (file->state, name, &tmp_type, NULL);
    g_return_if_fail (file->state && tmp_type == end);

    return type;
}

void
sfile_add_string       (SFileOutput       *file,
                        const char *name,
                        const char  *string)
{
    sfile_add_value (file, name, BEGIN_STRING, STRING, END_STRING);
}

void
sfile_add_integer      (SFileOutput       *file,
                        const        char *name,
                        int          integer)
{
    sfile_add_value (file, name, BEGIN_INTEGER, INTEGER, END_INTEGER);
}

void
sfile_add_pointer      (SFileOutput *file,
                        const char  *name,
                        gpointer     pointer)
{
    sfile_add_value (file, name, BEGIN_POINTER, POINTER, END_POINTER);
}

gboolean
sfile_save             (SFileOutput  *sfile,
                        const char   *filename,
                        GError      **err)
{
    return FALSE; /* FIXME */
}
