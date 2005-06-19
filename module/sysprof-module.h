/* Sysprof -- Sampling, systemwide CPU profiler
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

#ifndef SYSPROF_MODULE_H
#define SYSPROF_MODULE_H

typedef struct SysprofStackTrace SysprofStackTrace;

#define SYSPROF_MAX_ADDRESSES 512

struct SysprofStackTrace
{
    int	pid;		/* -1 if in kernel */
    int truncated;
    int n_addresses;	/* note: this can be 1 if the process was compiled
			 * with -fomit-frame-pointer or is otherwise weird
			 */
    void *addresses[SYSPROF_MAX_ADDRESSES];
};

#endif
