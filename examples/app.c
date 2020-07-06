/* This code is public domain */

/* The following is an example of how to use the SYSPROF_TRACE_FD
 * environment set by Sysprof to add marks or other information
 * to the capture from your application.
 */

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#ifdef __linux__
# include <sched.h>
#endif

#include <glib.h>
#include <signal.h>
#include <sysprof-capture.h>
#include <unistd.h>

static void do_some_work (SysprofCaptureWriter *);

int
main (int   argc,
      char *argv[])
{
  SysprofCaptureWriter *writer;

  /* Ignore SIGPIPE because we might get a pipe from the environment and we
   * don't want to trap if write() is used on it.
   */
  signal (SIGPIPE, SIG_IGN);

  /* This will check for SYSPROF_TRACE_FD=N, parse the FD number, and use it as
   * the backing file for the trace data. It may be a file, socket, pipe,
   * memfd, etc. The FD must not be used a second time.
   */
  writer = sysprof_capture_writer_new_from_env (0);

  /* Nothing to do */
  if (writer == NULL)
    {
      g_printerr ("SYSPROF_TRACE_FD not found, exiting.\n");
      return 1;
    }

  do_some_work (writer);

  sysprof_capture_writer_unref (writer);

  return 0;
}

static void
do_some_work (SysprofCaptureWriter *writer)
{
  const gint64 duration_usec = G_USEC_PER_SEC / 60L;

  /* Wait a few seconds to get started */
  g_usleep (G_USEC_PER_SEC * 3);

  for (guint i = 0; i < 120; i++)
    {
      gint64 begin_time_nsec = SYSPROF_CAPTURE_CURRENT_TIME;
      gint64 real_duration;
      gint64 end_time_nsec;

      g_usleep (duration_usec);

      end_time_nsec = SYSPROF_CAPTURE_CURRENT_TIME;
      real_duration = end_time_nsec - begin_time_nsec;

      sysprof_capture_writer_add_mark (writer,
                                       begin_time_nsec,            /* Begin time in nsec */
#ifdef __linux__
                                       sched_getcpu (),            /* -1 to ignore */
#else
                                       -1,
#endif
                                       getpid (),                  /* -1 to ignore */
                                       real_duration,              /* duration in nsec */
                                       "Example",                  /* Group name, 23 chars+\0 */
                                       "Sleep",                    /* Name, 39 chars+\0 */
                                       "Ancillary message data"); /* UTF-8 Message data, limited to
                                                                      64kb-frame size. */
    }
}
