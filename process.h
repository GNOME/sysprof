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
