#include "profile.h"

typedef struct Profiler Profiler;

typedef void (* ProfilerFunc) (gpointer data);

/* callback is called whenever a new sample arrives */
Profiler *profiler_new (ProfilerFunc callback,
			gpointer     data);
gboolean  profiler_start (Profiler *profiler,
			  GError   **err);
void      profiler_stop (Profiler *profiler);
void      profiler_reset (Profiler *profiler);
int	  profiler_get_n_samples (Profiler *profiler);

Profile * profiler_create_profile (Profiler *profiler);
