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

