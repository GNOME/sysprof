#include <string.h>
#include <stdlib.h>
#include <glib.h>
#include <stdarg.h>
#include "binparser.h"

typedef struct ParserFrame ParserFrame;

struct BinRecord
{
    BinFormat *		format;
    int			index;
    gsize		offset;
    BinParser *		parser;
};

struct BinField
{
    guint64	offset;
    int		width;
    int		align;
    char *	name;
};

struct BinFormat
{
    gboolean    big_endian;
    
    int		n_fields;
    BinField *	fields;
};

struct BinParser
{
    gsize		offset;
    const guchar *	data;
    gsize		length;

    gboolean		cache_in_use;
    BinRecord		cache;
};

BinParser *
bin_parser_new (const guchar	*data,
		gsize		 length)
{
    BinParser *parser = g_new0 (BinParser, 1);
    
    parser->offset = 0;
    parser->data = data;
    parser->length = length;
    parser->cache_in_use = FALSE;
    
    return parser;
}

void
bin_parser_free (BinParser *parser)
{
    g_free (parser);
}

static GQueue *
read_varargs (va_list		args,
	      const char *	name,
	      BinField *	field)
{
    GQueue *queue = g_queue_new ();
    gpointer p;
    
    if (name)
    {
	g_queue_push_tail (queue, (gpointer)name);
	g_queue_push_tail (queue, field);
	
	p = va_arg (args, gpointer);
	while (p)
	{
	    g_queue_push_tail (queue, p);
	    p = va_arg (args, gpointer);
	}
    }
    
    return queue;
}

static guint64
align (guint64 offset, int alignment)
{
    /* Note that we can speed this up by assuming alignment'
     * is a power of two, since
     *
     *     offset % alignment == offset & (alignemnt - 1)
     *
     */
    
    if (offset % alignment != 0)
	offset += (alignment - (offset % alignment));
    
    return offset;
}

gsize
bin_format_get_size (BinFormat *format)
{
    BinField *last_field = &(format->fields[format->n_fields - 1]);
    BinField *first_field = &(format->fields[0]);

    return align (last_field->offset + last_field->width, first_field->width);
}

BinFormat *
bin_format_new (gboolean big_endian,
		const char *name, BinField *field,
		...)
{
    GQueue *queue = g_queue_new ();
    BinFormat *format = g_new0 (BinFormat, 1);
    GList *list;
    int i;
    guint64 offset;
    va_list args;
    
    format->big_endian = big_endian;
    
    /* Build queue of child types */
    va_start (args, field);
    queue = read_varargs (args, name, field);
    va_end (args);
    
    g_assert (queue->length % 2 == 0);
    
    format->n_fields = queue->length / 2;
    format->fields = g_new (BinField, format->n_fields);
    
    i = 0;
    offset = 0;
    for (list = queue->head; list != NULL; list = list->next->next)
    {
	const char *name = list->data;
	BinField *field = list->next->data;
	
	offset = align (offset, field->align);
	
	format->fields[i].name = g_strdup (name);
	format->fields[i].width = field->width;
	format->fields[i].offset = offset;
	
	offset += field->width;
	++i;
	
	g_free (field);
    }
    
    g_queue_free (queue);
    
    return format;
}

static const BinField *
get_field (BinFormat *format,
	   const gchar *name)
{
    int i;
    
    for (i = 0; i < format->n_fields; ++i)
    {
	BinField *field = &(format->fields[i]);
	
	if (strcmp (field->name, name) == 0)
	    return field;
    }
    
    return NULL;
}

static guint64
convert_uint (const guchar *data,
	      gboolean	    big_endian,
	      int	    width)
{
    guint8 r8;
    guint16 r16;
    guint32 r32;
    guint64 r64;

#if 0
    if (width == 4)
	g_print ("converting at %p  %d %d %d %d\n", data, data[0], data[1], data[2], data[3]);
#endif
    
    switch (width)
    {
    case 1:
	r8 = *(guint8 *)data;
	return r8;
	
    case 2:
	r16 = *(guint16 *)data;
	
	if (big_endian)
	    r16 = GUINT16_FROM_BE (r16);
	else
	    r16 = GUINT16_FROM_LE (r16);
	
	return r16;
	
    case 4:
	r32 = *(guint32 *)data;
	
	if (big_endian)
	    r32 = GUINT32_FROM_BE (r32);
	else
	    r32 = GUINT32_FROM_LE (r32);
	
	return r32;
	
    case 8:
	r64 = *(guint64 *)data;
	
	if (big_endian)
	    r64 = GUINT64_FROM_BE (r64);
	else
	    r64 = GUINT64_FROM_LE (r64);
	
	return r64;

    default:
	g_assert_not_reached();
	return 0;
    }
}

guint32
bin_parser_get_uint32 (BinParser *parser)
{
    guint32 result;
    
    /* FIXME: This is broken for two reasons:
     *
     * (1) It assumes file_endian==machine_endian
     *
     * (2) It doesn't check for file overrun.
     *
     */
    result = *(guint32 *)(parser->data + parser->offset);

    parser->offset += 4;
    
    return result;
}

static BinField *
new_field_uint (int width)
{
    BinField *field = g_new0 (BinField, 1);
    
    field->width = width;
    field->align = width;
    
    return field;
}

BinField *
bin_field_new_uint8 (void)
{
    return new_field_uint (1);
}

BinField *
bin_field_new_uint16 (void)
{
    return new_field_uint (2);
}

BinField *
bin_field_new_uint32 (void)
{
    return new_field_uint (4);
}

BinField *
bin_field_new_uint64 (void)
{
    return new_field_uint (8);
}

const gchar *
bin_parser_get_string (BinParser *parser)
{
    const char *result;
    
    /* FIXME: check that the string is within the file */
    
    result = (const char *)parser->data + parser->offset;

    parser->offset += strlen (result) + 1;

    return result;
}

void
bin_parser_align (BinParser *parser,
		  gsize	     byte_width)
{
    parser->offset = align (parser->offset, byte_width);
}

void
bin_parser_goto (BinParser *parser,
		 gsize	    offset)
{
    parser->offset = offset;
}

BinParser *
bin_record_get_parser (BinRecord *record)
{
    return record->parser;
}

const gchar *
bin_record_get_string_indirect (BinRecord *record,
				const char *name,
				gsize str_table)
{
    BinParser *parser = record->parser;
    const char *result = NULL;    
    gsize index;
    gsize saved_offset;

    saved_offset = bin_parser_get_offset (record->parser);
    
    index = bin_record_get_uint (record, name);

    bin_parser_goto (record->parser, str_table + index);
    
    result = bin_parser_get_string (parser);

    bin_parser_goto (record->parser, saved_offset);

    return result;
}

gsize
bin_parser_get_offset (BinParser *parser)
{
    g_return_val_if_fail (parser != NULL, 0);

    return parser->offset;
}

const guchar *
bin_parser_get_data (BinParser *parser)
{
    return parser->data;
}

gsize
bin_parser_get_length (BinParser *parser)
{
    return parser->length;
}


/* Record */
BinRecord *
bin_parser_get_record (BinParser *parser,
		       BinFormat *format,
		       gsize      offset)
{
    BinRecord *record;
    
    if (!parser->cache_in_use)
    {
	parser->cache_in_use = TRUE;
	record = &(parser->cache);
    }
    else
    {
	record = g_new0 (BinRecord, 1);
    }

    record->parser = parser;
    record->index = 0;
    record->offset = offset;
    record->format = format;

    return record;
}

void
bin_record_free (BinRecord *record)
{
    if (record == &(record->parser->cache))
	record->parser->cache_in_use = FALSE;
    else
	g_free (record);
}

guint64
bin_record_get_uint (BinRecord *record,
		     const char *name)
{
    const guint8 *pos;
    const BinField *field;

    field = get_field (record->format, name);
    pos = record->parser->data + record->offset + field->offset;

#if 0
    g_print ("   record offset: %d\n", record->offset);
    g_print ("   record index: %d\n", record->index);
    g_print ("   field offset %d\n", field->offset);
#endif
    
    if (record->offset + field->offset + field->width > record->parser->length)
    {
	/* FIXME: generate error */
	return 0;
    }

#if 0
    g_print ("    uint %d at %p    =>   %d\n", field->width, pos, convert_uint (pos, record->format->big_endian, field->width));
#endif
    
    return convert_uint (pos, record->format->big_endian, field->width);
}

void
bin_record_index (BinRecord *record,
		  int	  index)
{
    gsize format_size = bin_format_get_size (record->format);
    
    record->offset -= record->index * format_size;
    record->offset += index * format_size;
    record->index = index;
}

gsize
bin_record_get_offset (BinRecord *record)
{
    return record->offset;
}

/* Fields */

BinField *
bin_field_new_fixed_array (int n_elements,
			   int element_size)
{
    BinField *field = g_new0 (BinField, 1);
    field->width = n_elements * element_size;
    field->align = element_size;
    return field;
}
