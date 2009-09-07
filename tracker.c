#include <glib.h>
#include "tracker.h"
#include "stackstash.h"

typedef struct new_process_t new_process_t;
typedef struct new_map_t new_map_t;
typedef struct sample_t sample_t;
typedef union  time_line_entry_t time_line_entry_t;

struct new_process_t
{
    pid_t	pid;
    char	command_line[64];
};

struct new_map_t
{
    char	file_name[PATH_MAX];
    pid_t	pid;
    uint64_t	start;
    uint64_t	end;
    uint64_t	offset;
    uint64_t	inode;
};

struct sample_t
{
    pid_t		 pid;
    StackNode		*trace;
};

union time_line_entry_t
{
    new_process_t	new_process;
    new_map_t		new_map;
    sample_t		sample;
};

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
