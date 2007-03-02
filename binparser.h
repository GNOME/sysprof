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
#include <glib.h>

typedef struct BinParser BinParser;
typedef struct BinRecord BinRecord;
typedef struct BinField  BinField;

/* The model is:
 *
 *    BinParser has an offset associated with it. This offset can be
 *    manipulated with methods
 *
 *		goto		- go to absolute position from file start
 *		goto_rel	- go to relative positio
 *		goto_record_rel	- skip the given number of records 
 *		align		- move forward until aligned to given width
 *		save/restore	- save/restore the current offset (stack)
 *
 *   and queried with
 *
 *		get_offset	- return current offset in bytes from start
 *
 *   data can be retrieved with
 *
 *		get_uint         - return a uint of given width, and skip
 *		get_string	 - return a null terminated stringm, and skip
 *		get_pstring	 - return a 'pascal' string with given length
 *
 *		get_uint_field	 - return the named field
 *
 *   formats should probably be definable as static data.
 *
 *   A bin parser also has an associated "status" with it. This can be
 *   OK, or error. It is ok to use a parser with an error status, but
 *   the data returned will not be meaningfull.
 *
 *   
 */

#define BIN_MAX_NAME		52

typedef enum
{
    BIN_LITTLE_ENDIAN,
    BIN_BIG_ENDIAN,
    BIN_NATIVE_ENDIAN
} BinEndian;

typedef enum
{
    /* More types can (and probably will) be added in the future */
    BIN_UINT,
    BIN_UNINTERPRETED
} BinType;

struct BinField {
    const char		name[BIN_MAX_NAME];
    char		type;
    char		n_bytes;	/* number of bytes in the type */
};

BinParser *   bin_parser_new        (const guchar *data,
				     gsize         length);
void          bin_parser_free       (BinParser    *parser);
const guchar *bin_parser_get_data   (BinParser    *parser);
gsize         bin_parser_get_length (BinParser    *parser);
void	      bin_parser_set_endian    (BinParser *parser,
					BinEndian  endian);
gboolean      bin_parser_error	       (BinParser *parser); 
void	      bin_parser_clear_error   (BinParser *parser);
const gchar * bin_parser_get_error_msg (BinParser *parser);
BinRecord *   bin_parser_create_record (BinParser      *parser,
					const BinField *fields);
gsize	      bin_record_get_size      (BinRecord *record);
guint64	      bin_parser_get_uint_field (BinParser *parser,
					 BinRecord *record,
					 const char *field);

/* Move current offset */
gsize	      bin_parser_get_offset  (BinParser  *parser);
void          bin_parser_set_offset  (BinParser  *parser,
				      gsize       offset);
void          bin_parser_align       (BinParser  *parser,
				      gsize       byte_width);
void	      bin_parser_seek_record (BinParser  *parser,
				      BinRecord  *record,
				      int	  n_records);
void	      bin_parser_save	    (BinParser    *parser);
void	      bin_parser_restore    (BinParser    *parser);

/* retrieve data */
guint64	      bin_parser_get_uint   (BinParser	  *parser,
				     int	   width);
const char *  bin_parser_get_string (BinParser    *parser);
