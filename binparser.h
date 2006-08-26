#include <glib.h>

typedef struct BinField BinField;
typedef struct BinFormat BinFormat;
typedef struct BinParser BinParser;
typedef struct BinRecord BinRecord;

/* BinParser */
BinParser *bin_parser_new (const guchar	*data,
			   gsize	length);
const guchar *bin_parser_get_data (BinParser *parser);
gsize bin_parser_get_length (BinParser *parser);
gsize bin_parser_get_offset (BinParser *parser);
#if 0
void bin_parser_index (BinParser *parser, int index);
void bin_parser_begin (BinParser *parser,
		       BinFormat *format,
		       gsize offset);
void bin_parser_end (BinParser *parser);
guint64 bin_parser_get_uint (BinParser *parser,
			     const gchar *name);
#endif
const char *bin_parser_get_string (BinParser *parser);

/* Record */
BinRecord *bin_parser_get_record (BinParser *parser,
				  BinFormat *format,
				  gsize      offset);
void bin_record_free (BinRecord *record);
guint64 bin_record_get_uint (BinRecord *record,
			  const char *name);
void bin_record_index (BinRecord *record,
		       int	  index);
gsize bin_record_get_offset (BinRecord *record);
const gchar *bin_record_get_string_indirect (BinRecord *record,
					     const char *name,
					     gsize str_table);
BinParser *bin_record_get_parser (BinRecord *record);

/* BinFormat */
BinFormat *bin_format_new (gboolean big_endian,
			   const char *name, BinField *field,
			   ...);
gsize bin_format_get_size (BinFormat *format);

/* BinField */
BinField *bin_field_new_uint8 (void);
BinField *bin_field_new_uint16 (void);
BinField *bin_field_new_uint32 (void);
BinField *bin_field_new_uint64 (void);
BinField *bin_field_new_fixed_array (int n_elements,
				     int element_size);
