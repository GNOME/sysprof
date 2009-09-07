#include <glib.h>
#include "tracker.h"

struct tracker_t
{
    
};

tracker_t *
tracker_new (void)
{
}

void
tracker_free (tracker_t *tracker)
{
    
}

void
tracker_add_process (tracker_t *tracker)
{
}

void
tracker_add_map     (tracker_t *tracker)
{
}

void
tracker_add_sample  (tracker_t *tracker,
		     pid_t	pid,
		     uint64_t  *ips,
		     int        n_ips)
{
}

Profile *
tracker_create_profile (tracker_t *tracker)
{
    
}
