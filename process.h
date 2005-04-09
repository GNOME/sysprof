/* MemProf -- memory profiler and leak detector
 * Copyright 1999, 2000, 2001, Red Hat, Inc.
 * Copyright 2002, Kristian Rietveld
 *
 * Sysprof -- Sampling, systemwide CPU profiler
 * Copyright 2004, Red Hat, Inc
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

#ifndef PROCESS_H
#define PROCESS_H

#include "binfile.h"

typedef struct Process Process;

/* We are making the assumption that pid's are not recycled during
 * a profiling run. That is wrong, but necessary to avoid reading
 * from /proc/<pid>/maps on every sample (which would be a race
 * condition anyway).
 *
 * If the address passed to new_from_pid() is somewhere that hasn't
 * been checked before, the mappings are reread for the Process. This
 * means that if some previously sampled pages have been unmapped,
 * they will be lost and appear as "???" on the profile.
 *
 * To flush the pid cache, call process_flush_caches().
 * This will invalidate all instances of Process.
 *
 */

void          process_flush_caches                (void);
Process *     process_get_from_pid                (int         pid);
void          process_ensure_map                  (Process    *process,
						   int         pid,
						   gulong      address);
const Symbol *process_lookup_symbol               (Process    *process,
						   gulong      address);
const Symbol *process_lookup_symbol_with_filename (Process    *process,
						   int	       pid,
						   gulong      map_start,
						   const char *filename,
						   gulong      address);
const char *  process_get_cmdline                 (Process    *process);


#endif
