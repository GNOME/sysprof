#include <glib.h>

typedef struct BinField BinField;
typedef struct BinFormat BinFormat;
typedef struct BinParser BinParser;
BinParser *bin_parser_new (const guchar	*data,
			   gsize	length);
BinFormat *bin_format_new (gboolean big_endian,
			   const char *name, BinField *field,
			   ...);
gsize bin_format_get_size (BinFormat *format);
void bin_parser_index (BinParser *parser, int index);
void bin_parser_begin (BinParser *parser,
		       BinFormat *format,
		       gsize offset);
void bin_parser_end (BinParser *parser);
const char *bin_parser_get_string (BinParser *parser);
guint64 bin_parser_get_uint (BinParser *parser,
			     const gchar *name);
BinField *bin_field_new_uint8 (void);
BinField *bin_field_new_uint16 (void);
BinField *bin_field_new_uint32 (void);
BinField *bin_field_new_uint64 (void);
BinField *bin_field_new_fixed_array (int n_elements,
			    int element_size);
