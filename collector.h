#include "profile.h"

typedef struct Collector Collector;

typedef void (* CollectorFunc) (gpointer data);

/* callback is called whenever a new sample arrives */
Collector *collector_new (CollectorFunc callback,
			gpointer     data);
gboolean  collector_start (Collector *collector,
			  GError   **err);
void      collector_stop (Collector *collector);
void      collector_reset (Collector *collector);
int	  collector_get_n_samples (Collector *collector);

Profile * collector_create_profile (Collector *collector);
