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

enum
{
    TYPE_POINTER,
    TYPE_STRING,
    TYPE_INTEGER,
    TYPE_GENERIC_RECORD,
    TYPE_GENERIC_LIST,
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
transition_new (const char *element,
                TransitionKind kind,
                SType type,
                State *from,
                State *to)
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

void
sformat_free (SFormat *format)
{
    /* FIXME */
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
 *     without losing precision? Gnumeric may know.
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
    return sformat_new_value (name, TYPE_POINTER);
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
                        SType *type,
                        GError **err)
{
    GList *list;
    
    for (list = state->transitions->head; list; list = list->next)
    {
        Transition *transition;

        if (transition->kind == kind                    &&
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
                        SType *type, GError **err)
{
    return state_transition_check (state, element, BEGIN, type, err);
}

static const State *
state_transition_end (const State *state, const char *element,
                      SType *type, GError **err)
{
    return state_transition_check (state, element, END, type, err);
}

static const State *
state_transition_text (const State *state, SType *type, GError **err)
{
    GList *list;
    
    for (list = state->transitions->head; list; list = list->next)
    {
        Transition *transition = list->data;
        
        if (transition->kind == VALUE)
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
            int n_instructions;
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
            int target_id;
            Instruction *target_instruction;
            gpointer *location;
            gpointer object;
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
    int n_instructions;
    Instruction *instructions;
    Instruction *current_instruction;
    GHashTable *instructions_by_location;
};

static gboolean
is_record_type (SType type)
{
    /* FIXME */
    return TRUE;
}

static gboolean
is_list_type (SType type)
{
    /* FIXME */
    return FALSE;
}

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
    
    return instruction->u.begin.n_instructions;
}

void
sfile_get_pointer (SFileInput  *file,
                   const char *name,
                   gpointer    *location)
{
    Instruction *instruction = file->current_instruction++;
    g_return_if_fail (instruction->type == TYPE_POINTER &&
                      strcmp (instruction->name, name) == 0);
    
    instruction->u.pointer.location = location;

    *location = (gpointer) 0xFedeAbe;

    if (location)
    {
        if (g_hash_table_lookup (file->instructions_by_location, location))
            g_warning ("Reading into the same location twice\n");
        
        g_hash_table_insert (file->instructions_by_location, location, instruction);
    }
}

void
sfile_get_integer      (SFileInput  *file,
                        const char  *name,
                        int         *integer)
{
    Instruction *instruction = file->current_instruction++;
    g_return_if_fail (instruction->type == TYPE_INTEGER &&
                      strcmp (instruction->name, name) == 0);
    
    if (integer)
        *integer = instruction->u.integer.value;
}

void
sfile_get_string       (SFileInput  *file,
                        const char  *name,
                        char       **string)
{
    Instruction *instruction = file->current_instruction++;
    g_return_if_fail (instruction->type == TYPE_STRING &&
                      strcmp (instruction->name, name) == 0);
    
    if (string)
        *string = g_strdup (instruction->u.string.value);
}

static void
hook_up_pointers (SFileInput *file)
{
    int i;

    for (i = 0; i < file->n_instructions; ++i)
    {
        Instruction *instruction = &(file->instructions[i]);

        if (instruction->type == TYPE_POINTER)
        {
            gpointer target_object =
                instruction->u.pointer.target_instruction->u.begin.end_instruction->u.end.object;

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
    
    g_return_if_fail (instruction->kind == END &&
                      strcmp (instruction->name, name) == 0);

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

    build->state = state_transition_begin (build->state, element_name, &instruction.type, err);
    instruction.name = g_strdup (element_name);
    
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
    
    build->state = state_transition_end (build->state, element_name, &instruction.type, err);

    instruction.name = g_strdup (element_name);

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
    
    build->state = state_transition_text (build->state, &instruction.type, err);

    instruction.name = NULL;

    switch (instruction.type)
    {
    case TYPE_POINTER:
        if (!get_number (text, &instruction.u.pointer.target_id))
        {
            set_invalid_content_error (err, "Contents '%s' of pointer element is not a number", text);
            return;
        }
        break;

    case TYPE_INTEGER:
        if (!get_number (text, &instruction.u.integer.value))
        {
            set_invalid_content_error (err, "Contents '%s' of integer element not a number", text);
            return;
        }
        break;

    case TYPE_STRING:
        if (!decode_text (text, &instruction.u.string.value))
        {
            set_invalid_content_error (err, "Contents '%s' of text element is illformed", text);
            return;
        }
        break;

    default:
        g_assert_not_reached();
        break;
    }

    g_array_append_val (build->instructions, instruction);
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
        
        if (instruction->type == TYPE_STRING)
            g_free (instruction->u.string.value);
    }
    
    g_free (instructions);
}

/* This functions counts the number of instructions in each list, and
 * matches up pointers with the lists/records they point to.
 * FIMXE: think of a better name
 */
static Instruction *
post_process_instructions_recurse (Instruction *first, GHashTable *instructions_by_id, GError **err)
{
    Instruction *instruction;
    int n_instructions;

    g_assert (first->kind == BEGIN);
    
    instruction = first + 1;

    n_instructions = 0;
    while (instruction->kind != END)
    {
        if (instruction->kind == BEGIN)
        {
            instruction = post_process_instructions_recurse (instruction, instructions_by_id, err);
            if (!instruction)
                return NULL;
        }
        else
        {
            if (instruction->type == TYPE_POINTER)
            {
                int target_id = instruction->u.pointer.target_id;
                Instruction *target = g_hash_table_lookup (instructions_by_id, GINT_TO_POINTER (target_id));

                if (!target)
                {
                    set_invalid_content_error (err, "Id %d doesn't reference any record or list\n",
                                               instruction->u.pointer.target_id);
                    return NULL;
                }

                instruction->u.pointer.target_instruction = target;
            }
            
            instruction++;
        }
        
        n_instructions++;
    }

    first->u.begin.n_instructions = n_instructions;
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

    /* count list instructions, check pointers */
    if (!post_process_instructions_recurse (instructions, instructions_by_id, err))
        retval = FALSE;
    
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
    if (!sformat_is_end_state (format, build.state))
    {
        set_invalid_content_error (err, "Unexpected end of file\n");

        free_instructions ((Instruction *)build.instructions->data, build.instructions->len);
        return NULL;
    }
    
    if (!g_markup_parse_context_parse (parse_context, contents, -1, err))
    {
        free_instructions ((Instruction *)build.instructions->data, build.instructions->len);
	return NULL;
    }

    if (!post_process_read_instructions ((Instruction *)build.instructions->data, build.instructions->len, err))
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
    
    return input;
}

/* Writing */
struct SFileOutput
{
    SFormat *format;
    GArray *instructions;
    const State *state;
};


SFileOutput *
sfile_output_new (SFormat       *format)
{
    SFileOutput *output = g_new (SFileOutput, 1);
    
    output->format = format;
    output->instructions = g_array_new (TRUE, TRUE, sizeof (Instruction));
    output->state = sformat_get_start_state (format);

    return output;
}

void
sfile_begin_add_record (SFileOutput       *file,
                        const char        *name)
{
    Instruction instruction;
    
    file->state = state_transition_begin (
        file->state, name, &instruction.type, NULL);

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

    file->state = state_transition_begin (
        file->state, name, &instruction.type, NULL);

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

    file->state = state_transition_end (
        file->state, name, &instruction.type, NULL);

    g_return_if_fail (file->state && instruction.kind == END);

    instruction.kind = END;
    instruction.name = g_strdup (name);
    instruction.u.end.object = object;

    g_array_append_val (file->instructions, instruction);
}

static void
sfile_check_value (SFileOutput *file,
                   const char *name,
                   SType type)
{
    SType tmp_type;
    
    file->state = state_transition_begin (file->state, name, &tmp_type, NULL);
    g_return_if_fail (file->state && tmp_type == type);

    file->state = state_transition_text (file->state, &type, NULL);
    g_return_if_fail (file->state && tmp_type == type);
    
    file->state = state_transition_end (file->state, name, &type, NULL);
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
    instruction.u.pointer.object = pointer;

    g_array_append_val (file->instructions, instruction);
}

gboolean
sfile_output_save (SFileOutput  *sfile,
                   const char   *filename,
                   GError      **err)
{
    
    return FALSE; /* FIXME */
}


void
sfile_output_free (SFileOutput *sfile)
{
    /* FIXME */
}
