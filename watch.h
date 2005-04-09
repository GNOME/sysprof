/*   - Library for asynchronous communication
 *  Copyright (C) 2002  Søren Sandmann (sandmann@daimi.au.dk)
 *
 *  This library is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <glib.h>

/*
 * Watching file descriptors
 */
typedef void (* WatchCallback) (gpointer data);

void     fd_add_watch             (gint             fd,
				   gpointer         data);
void     fd_set_read_callback     (gint             fd,
				   WatchCallback read_cb);
void     fd_set_write_callback    (gint             fd,
				   WatchCallback write_cb);
void     fd_set_hangup_callback   (gint             fd,
				   WatchCallback hangup_cb);
void     fd_set_error_callback    (gint             fd,
				   WatchCallback error_cb);
void     fd_set_priority_callback (gint             fd,
				   WatchCallback priority_cb);
void     fd_remove_watch          (gint             fd);
gboolean fd_is_watched            (gint             fd);

