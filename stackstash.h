#ifndef STACK_STASH_H
#define STACK_STASH_H

#include <glib.h>
#include "process.h"

typedef struct StackStash StackStash;

typedef void (* StackFunction) (Process *process,
				GSList *trace,
				gint size,
				gpointer data);

/* Stach */
StackStash *stack_stash_new       (void);
void        stack_stash_add_trace (StackStash      *stash,
				   Process	   *process,
				   gulong          *addrs,
				   gint	            n_addrs,
				   int              size);
void        stack_stash_foreach   (StackStash      *stash,
				   StackFunction    stack_func,
				   gpointer         data);
void	    stack_stash_free	  (StackStash	   *stash);

#endif
