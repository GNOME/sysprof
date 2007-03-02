/* Sysprof -- Sampling, systemwide CPU profiler
 * Copyright 2006, 2007, Soeren Sandmann
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
#include <string.h>

#include "binparser.h"

typedef struct Field Field;

struct BinParser
{
    const guchar *	data;
    gsize		length;

    gsize		offset;
    const char *	error_msg;
    GList *		records;
    BinEndian		endian;

    gsize		saved_offset;
};

struct Field
{
    char	name[BIN_MAX_NAME];
    guint	offset;			/* from beginning of struct */
    guint	width;
    BinType	type;
};

struct BinRecord
{
    int			n_fields;
    Field		fields[1];
};

BinParser *
bin_parser_new (const guchar *data,
		gsize	      length)
{
    BinParser *parser = g_new0 (BinParser, 1);
    
    parser->data = data;
    parser->length = length;
    parser->offset = 0;
    parser->error_msg = NULL;
    parser->records = NULL;
    parser->endian = BIN_NATIVE_ENDIAN;
    
    return parser;
}

void
bin_parser_free (BinParser *parser)
{
    GList *list;

    for (list = parser->records; list != NULL; list = list->next)
    {
	BinRecord *record = list->data;
	
	g_free (record);
    }

    g_list_free (parser->records);
    
    g_free (parser);
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

static int
get_align (const BinField *field)
{
    if (field->type == BIN_UNINTERPRETED)
	return 1;
    else
	return field->n_bytes;
}

BinRecord *
bin_parser_create_record (BinParser      *parser,
			  const BinField *fields)
{
    BinRecord *record;
    int i, n_fields;
    guint offset;
	
    n_fields = 0;
    while (fields[n_fields].name[0] != '\0')
    {
	n_fields++;
#if 0
	g_print ("type: %d\n", fields[n_fields].type);
#endif
    }

    record = g_malloc0 (sizeof (BinRecord) +
			(n_fields - 1) * sizeof (Field));

    offset = 0;
    record->n_fields = n_fields;
    for (i = 0; i < n_fields; ++i)
    {
	const BinField *bin_field = &(fields[i]);
	Field *field = &(record->fields[i]);
	
	offset = align (offset, get_align (bin_field));
	
	strncpy (field->name, bin_field->name, BIN_MAX_NAME - 1);
	field->offset = offset;
	field->type = bin_field->type;
	field->width = bin_field->n_bytes;

	offset += record->fields[i].width;
    }

    parser->records = g_list_prepend (parser->records, record);
    
    return record;
}

gboolean
bin_parser_error (BinParser *parser)
{
    return parser->error_msg != NULL;
}

void
bin_parser_clear_error (BinParser *parser)
{
    parser->error_msg = NULL;
}

const gchar *
bin_parser_get_error_msg (BinParser *parser)
{
    return parser->error_msg;
}

void
bin_parser_set_endian (BinParser *parser,
		       BinEndian  endian)
{
    parser->endian = endian;
}

/* Move current offset */
gsize
bin_parser_get_offset  (BinParser  *parser)
{
    return parser->offset;
}

void
bin_parser_set_offset  (BinParser  *parser,
			gsize       offset)
{
    parser->offset = offset;
}

void
bin_parser_align       (BinParser  *parser,
			gsize       byte_width)
{
    parser->offset = align (parser->offset, byte_width);
}

gsize
bin_record_get_size (BinRecord *record)
{
    Field *last_field = &(record->fields[record->n_fields - 1]);
    Field *first_field = &(record->fields[0]);

    /* align to first field, since that's the alignment of the record
     * following this one
     */
    
    return align (last_field->offset + last_field->width, first_field->width);
}

void
bin_parser_seek_record (BinParser  *parser,
			BinRecord  *record,
			int	  n_records)
{
    gsize record_size = bin_record_get_size (record);

    parser->offset += record_size * n_records;
}

void
bin_parser_save (BinParser *parser)
{
    parser->saved_offset = parser->offset;
}

void
bin_parser_restore (BinParser *parser)
{
    parser->offset = parser->saved_offset;
}

/* retrieve data */
static guint64
convert_uint (const guchar *data,
	      BinEndian	    endian,
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

    /* FIXME: check that we are within the file */
    
    switch (width)
    {
    case 1:
	r8 = *(guint8 *)data;
	return r8;
	
    case 2:
	r16 = *(guint16 *)data;
	
	if (endian == BIN_BIG_ENDIAN)
	    r16 = GUINT16_FROM_BE (r16);
	else if (endian == BIN_LITTLE_ENDIAN)
	    r16 = GUINT16_FROM_LE (r16);
	
	return r16;
	
    case 4:
	r32 = *(guint32 *)data;
	
	if (endian == BIN_BIG_ENDIAN)
	    r32 = GUINT32_FROM_BE (r32);
	else if (endian == BIN_LITTLE_ENDIAN)
	    r32 = GUINT32_FROM_LE (r32);
	
	return r32;
	
    case 8:
	r64 = *(guint64 *)data;
	
	if (endian == BIN_BIG_ENDIAN)
	    r64 = GUINT64_FROM_BE (r64);
	else if (endian == BIN_LITTLE_ENDIAN)
	    r64 = GUINT64_FROM_LE (r64);
	
	return r64;

    default:
	g_assert_not_reached();
	return 0;
    }
}

guint64
bin_parser_get_uint (BinParser *parser,
		     int	width)
{
    guint64 r = convert_uint (parser->data + parser->offset, parser->endian, width);

    parser->offset += width;

    return r;
}

const char *
bin_parser_get_string (BinParser *parser)
{
    const char *result;
    
    /* FIXME: check that the string is within the file */
    
    result = (const char *)parser->data + parser->offset;

    parser->offset += strlen (result) + 1;

    return result;
    
}

static const Field *
get_field (BinRecord *format,
	   const gchar *name)
{
    int i;
    
    for (i = 0; i < format->n_fields; ++i)
    {
	Field *field = &(format->fields[i]);

	if (strcmp (field->name, name) == 0)
	{
#if 0
	    g_print ("found field: %s  (offset: %d, type %d)\n", field->name, field->offset, field->type);
#endif
	    
	
	    return field;
	}
    }
    
    return NULL;
}

guint64
bin_parser_get_uint_field (BinParser *parser,
			   BinRecord *record,
			   const char *name)
{
    const Field *field = get_field (record, name);
    const guchar *pos;

#if 0
    g_print ("moving to %d (%d + %d)\n", parser->offset + field->offset, parser->offset, field->offset);
#endif
    
    pos = parser->data + parser->offset + field->offset;

#if 0
    g_print ("   record offset: %d\n", record->offset);
    g_print ("   record index: %d\n", record->index);
    g_print ("   field offset %d\n", field->offset);
#endif
    
    if (pos > parser->data + parser->length)
    {
	/* FIXME: generate error */
	return 0;
    }

#if 0
    g_print ("    uint %d at %p    =>   %d\n", field->width, pos, convert_uint (pos, record->format->big_endian, field->width));
#endif
    
    return convert_uint (pos, parser->endian, field->width);
}
