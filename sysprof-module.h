#ifndef SYSPROF_MODULE_H
#define SYSPROF_MODULE_H

typedef struct SysprofStackTrace SysprofStackTrace;

#define SYSPROF_MAX_ADDRESSES 1024

struct SysprofStackTrace
{
    int	pid;
    int truncated;
    int n_addresses;	/* note: this can be 1 if the process was compiled
			 * with -fomit-frame-pointer or is otherwise weird
			 */
    void *addresses[SYSPROF_MAX_ADDRESSES];

    char filename[8192];
    int map_start;
};

#endif
