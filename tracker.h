#include <stdint.h>
#include "profile.h"

typedef struct tracker_t tracker_t;

tracker_t *tracker_new (void);
void	   tracker_free (tracker_t *);

void tracker_add_process (tracker_t  *tracker,
			  pid_t	      pid,
			  const char *command_line);
void tracker_add_fork (tracker_t *tracker,
		       pid_t      pid,
		       pid_t	  child_pid);
void tracker_add_exit (tracker_t *tracker,
		       pid_t      pid);
void tracker_add_map (tracker_t * tracker,
		      pid_t	     pid,
		      uint64_t    start,
		      uint64_t    end,
		      uint64_t    offset,
		      uint64_t    inode,
		      const char *filename);
void tracker_add_sample  (tracker_t *tracker,
			  pid_t	     pid,
			  uint64_t  *ips,
			  int        n_ips);

Profile *tracker_create_profile (tracker_t *tracker);
