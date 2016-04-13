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

#ifndef PROFILE_H
#define PROFILE_H

#include <glib.h>
#include "stackstash.h"

typedef struct Profile Profile;
typedef struct ProfileObject ProfileObject;
typedef struct ProfileDescendant ProfileDescendant;
typedef struct ProfileCaller ProfileCaller;

struct ProfileObject
{
    char *		name;		/* identifies this object uniquely
					 * (the pointer itself, not the
					 *  string)
					 */
    
    guint		total;		/* sum of all toplevel totals */
    guint		self;		/* sum of all selfs */
};

struct ProfileDescendant
{
    char *		name;
    guint		self;
    guint		cumulative;
    ProfileDescendant * parent;
    ProfileDescendant * siblings;
    ProfileDescendant * children;
};

struct ProfileCaller
{
    char *		name;
    guint		total;
    guint		self;

    ProfileCaller *	next;
};

Profile *          profile_new                (StackStash        *stash);
void               profile_free               (Profile           *profile);
gint		   profile_get_size	      (Profile           *profile);
GList *		   profile_get_objects	      (Profile		 *profile);
ProfileDescendant *profile_create_descendants (Profile           *prf,
					       char		 *object);
ProfileCaller *	   profile_list_callers       (Profile		 *profile,
					       char              *object);
void		   profile_caller_free	      (ProfileCaller     *caller);
void               profile_descendant_free    (ProfileDescendant *descendant);

gboolean	   profile_save		      (Profile		 *profile,
					       const char	 *file_name,
					       GError		**err);
Profile *	   profile_load		      (const char	 *filename,
					       GError           **err);

#endif /* PROFILE_H */
