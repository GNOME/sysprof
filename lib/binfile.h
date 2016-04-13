/* MemProf -- memory profiler and leak detector
 * Copyright 1999, 2000, 2001, Red Hat, Inc.
 * Copyright 2002, Kristian Rietveld
 *
 * Sysprof -- Sampling, systemwide CPU profiler
 * Copyright 2004, Red Hat, Inc.
 * Copyright 2004, 2005, Soeren Sandmann
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

#ifndef BIN_FILE_H
#define BIN_FILE_H

#include <glib.h>
#include <sys/types.h>

typedef struct bin_file_t bin_file_t;
typedef struct bin_symbol_t bin_symbol_t;

/* Binary File */

bin_file_t *        bin_file_new           (const char      *filename);
void             bin_file_free          (bin_file_t         *bin_file);
const bin_symbol_t *bin_file_lookup_symbol (bin_file_t         *bin_file,
					 gulong           address);
gboolean         bin_file_check_inode   (bin_file_t         *bin_file,
					 ino_t		  inode);
const char *     bin_symbol_get_name    (bin_file_t         *bin_file,
					 const bin_symbol_t *symbol);
gulong		 bin_symbol_get_address (bin_file_t         *bin_file,
					 const bin_symbol_t *symbol);

#endif
