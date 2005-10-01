#include "stackstash.h"
#include "module/sysprof-module.h"

void
add_trace_to_stash (SysprofStackTrace *trace,
		    StackStash *stash)
{
    int i;
    gulong *addrs;
    Process *process = process_get_from_pid (trace->pid);
    
    addrs = g_new (gulong, trace->n_addresses + 1);

    for (i = 0; i < trace->n_addresses; ++i)
    {
	process_ensure_map (process, trace->pid, 
			    (gulong)trace->addresses[i]);

	addrs[i] = (gulong)trace->addresses[i];
    }

    addrs[i] = (gulong)process;
    
    stack_stash_add_trace (
	stash, addrs, trace->n_addresses + 1, 1);
    
    g_free (addrs);
}
