#ifndef BIN_FILE_H
#define BIN_FILE_H

#include <glib.h>

typedef struct BinFile BinFile;
typedef struct Symbol Symbol;

/* Binary File */

BinFile *     bin_file_new           (const char *filename);
void          bin_file_free          (BinFile    *bin_file);
const Symbol *bin_file_lookup_symbol (BinFile    *bin_file,
				      gulong      address);
gulong        bin_file_get_load_address (BinFile *bf);

/* Symbol */
struct Symbol
{
    char *	name;
    gulong	address;
};

Symbol * symbol_copy  (const Symbol *orig);
gboolean symbol_equal (const void *syma,
		       const void *symb);
guint    symbol_hash  (const void *sym);
void     symbol_free  (Symbol       *symbol);

#endif
