/* Sysprof -- Sampling, systemwide CPU profiler
 * Copyright 2007, Soeren Sandmann
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

typedef struct BinField BinField;
typedef struct BinFormat BinFormat;
typedef struct BinParser BinParser;
typedef struct BinRecord BinRecord;

/* BinParser */
BinParser *   bin_parser_new        (const guchar *data,
				     gsize         length);
void          bin_parser_free       (BinParser    *parser);
const guchar *bin_parser_get_data   (BinParser    *parser);
gsize         bin_parser_get_length (BinParser    *parser);
gsize         bin_parser_get_offset (BinParser    *parser);
void          bin_parser_align      (BinParser    *parser,
				     gsize         byte_width);
void          bin_parser_goto       (BinParser    *parser,
				     gsize         offset);
const char *  bin_parser_get_string (BinParser    *parser);
guint32       bin_parser_get_uint32 (BinParser    *parser);

/* Record */
BinRecord *  bin_parser_get_record          (BinParser  *parser,
					     BinFormat  *format,
					     gsize       offset);
void         bin_record_free                (BinRecord  *record);
guint64      bin_record_get_uint            (BinRecord  *record,
					     const char *name);
void         bin_record_index               (BinRecord  *record,
					     int         index);
gsize        bin_record_get_offset          (BinRecord  *record);
const gchar *bin_record_get_string_indirect (BinRecord  *record,
					     const char *name,
					     gsize       str_table);
BinParser *  bin_record_get_parser          (BinRecord  *record);


/* BinFormat */
BinFormat *bin_format_new      (gboolean    big_endian,
				const char *name,
				BinField   *field,
				...);
gsize      bin_format_get_size (BinFormat  *format);

/* BinField */
BinField *bin_field_new_uint8       (void);
BinField *bin_field_new_uint16      (void);
BinField *bin_field_new_uint32      (void);
BinField *bin_field_new_uint64      (void);
BinField *bin_field_new_fixed_array (int  n_elements,
				     int  element_size);
