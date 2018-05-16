/* sysprofd.c
 *
 * Copyright (C) 2016 Christian Hergert <christian@hergert.me>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <glib.h>
#include <linux/capability.h>
#include <linux/perf_event.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "sd-bus-helper.h"
#include "../lib/sp-kallsyms.h"

#define BUS_TIMEOUT_USEC (1000000L * 10L)

#if 0
#define GOTO(l) do { \
  fprintf (stderr, "GOTO: %s:%d: " #l "\n", __FUNCTION__, __LINE__); \
  goto l; \
} while (0)
#else
#define GOTO(l) goto l
#endif

static int
sysprofd_get_kernel_symbols (sd_bus_message *msg,
                             void           *user_data,
                             sd_bus_error   *error)
{
  g_autoptr(SpKallsyms) kallsyms = NULL;
  sd_bus_message *reply = NULL;
  const gchar *name;
  guint64 addr;
  guint8 type;
  bool challenge = false;
  int r;

  assert (msg);
  assert (error);

  /* Authorize peer */
  r = bus_test_polkit (msg,
                       CAP_SYS_ADMIN,
                       "org.gnome.sysprof2.get-kernel-symbols",
                       NULL,
                       UID_INVALID,
                       &challenge,
                       error);

  if (r <= 0)
    fprintf (stderr, "GetKernelSymbols() Failure: %s\n", error->message);

  if (r < 0)
    return r;
  else if (r == 0)
    return -EACCES;

  if (!(kallsyms = sp_kallsyms_new (NULL)))
    {
      sd_bus_error_set (error,
                        SD_BUS_ERROR_FILE_NOT_FOUND,
                        "Failed to open /proc/kallsyms");
      return -ENOENT;
    }

  r = sd_bus_message_new_method_return (msg, &reply);
  if (r < 0)
    return r;

  r = sd_bus_message_open_container (reply, 'a', "(tys)");
  if (r < 0)
    return r;

  while (sp_kallsyms_next (kallsyms, &name, &addr, &type))
    sd_bus_message_append (reply, "(tys)", addr, type, name);

  r = sd_bus_message_close_container (reply);
  if (r < 0)
    return r;

  r = sd_bus_send (NULL, reply, NULL);
  sd_bus_message_unref (reply);

  return r;
}

static int
_perf_event_open (struct perf_event_attr *attr,
                  pid_t                   pid,
                  int                     cpu,
                  int                     group_fd,
                  unsigned long           flags)
{
  assert (attr != NULL);

  /* Quick sanity check */
  if (attr->sample_period < 100000 && attr->type != PERF_TYPE_TRACEPOINT)
    return -EINVAL;

  return syscall (__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

static int
sysprofd_perf_event_open (sd_bus_message *msg,
                          void           *user_data,
                          sd_bus_error   *error)
{
  struct perf_event_attr attr = { 0 };
  sd_bus_message *reply = NULL;
  uint64_t flags = 0;
  int disabled = 0;
  int32_t wakeup_events = 149;
  int32_t cpu = -1;
  int32_t pid = -1;
  bool challenge = false;
  int32_t type = 0;
  uint64_t sample_period = 0;
  uint64_t sample_type = 0;
  uint64_t config = 0;
  int clockid = CLOCK_MONOTONIC_RAW;
  int comm = 0;
  int mmap_ = 0;
  int task = 0;
  int exclude_idle = 0;
  int fd = -1;
  int use_clockid = 0;
  int sample_id_all = 0;
  int r;

  assert (msg);

  r = sd_bus_message_enter_container (msg, SD_BUS_TYPE_ARRAY, "{sv}");
  if (r < 0)
    return r;

  for (;;)
    {
      const char *name = NULL;

      r = sd_bus_message_enter_container (msg, SD_BUS_TYPE_DICT_ENTRY, "sv");
      if (r < 0)
        return r;

      if (r == 0)
        break;

      r = sd_bus_message_read (msg, "s", &name);
      if (r < 0)
        goto cleanup;

      r = sd_bus_message_enter_container (msg, SD_BUS_TYPE_VARIANT, NULL);
      if (r < 0)
        goto cleanup;

      if (strcmp (name, "disabled") == 0)
        {
          r = sd_bus_message_read (msg, "b", &disabled);
          if (r < 0)
            GOTO (cleanup);
        }
      else if (strcmp (name, "wakeup_events") == 0)
        {
          r = sd_bus_message_read (msg, "u", &wakeup_events);
          if (r < 0)
            GOTO (cleanup);
        }
      else if (strcmp (name, "sample_id_all") == 0)
        {
          r = sd_bus_message_read (msg, "b", &sample_id_all);
          if (r < 0)
            GOTO (cleanup);
        }
      else if (strcmp (name, "clockid") == 0)
        {
          r = sd_bus_message_read (msg, "i", &clockid);
          if (r < 0)
            GOTO (cleanup);
        }
      else if (strcmp (name, "comm") == 0)
        {
          r = sd_bus_message_read (msg, "b", &comm);
          if (r < 0)
            GOTO (cleanup);
        }
      else if (strcmp (name, "exclude_idle") == 0)
        {
          r = sd_bus_message_read (msg, "b", &exclude_idle);
          if (r < 0)
            GOTO (cleanup);
        }
      else if (strcmp (name, "mmap") == 0)
        {
          r = sd_bus_message_read (msg, "b", &mmap_);
          if (r < 0)
            GOTO (cleanup);
        }
      else if (strcmp (name, "config") == 0)
        {
          r = sd_bus_message_read (msg, "t", &config);
          if (r < 0)
            GOTO (cleanup);
        }
      else if (strcmp (name, "sample_period") == 0)
        {
          r = sd_bus_message_read (msg, "t", &sample_period);
          if (r < 0)
            GOTO (cleanup);
        }
      else if (strcmp (name, "sample_type") == 0)
        {
          r = sd_bus_message_read (msg, "t", &sample_type);
          if (r < 0)
            GOTO (cleanup);
        }
      else if (strcmp (name, "task") == 0)
        {
          r = sd_bus_message_read (msg, "b", &task);
          if (r < 0)
            GOTO (cleanup);
        }
      else if (strcmp (name, "type") == 0)
        {
          r = sd_bus_message_read (msg, "u", &type);
          if (r < 0)
            GOTO (cleanup);
        }
      else if (strcmp (name, "use_clockid") == 0)
        {
          r = sd_bus_message_read (msg, "b", &use_clockid);
          if (r < 0)
            GOTO (cleanup);
        }

      r = sd_bus_message_exit_container (msg);
      if (r < 0)
        goto cleanup;

      sd_bus_message_exit_container (msg);
      if (r < 0)
        goto cleanup;

    cleanup:
      if (r < 0)
        return r;
    }

  r = sd_bus_message_exit_container (msg);
  if (r < 0)
    return r;

  r = sd_bus_message_read (msg, "iit", &pid, &cpu, &flags);
  if (r < 0)
    return r;

  if (pid < -1 || cpu < -1)
    return -EINVAL;

  r = sd_bus_message_new_method_return (msg, &reply);
  if (r < 0)
    return r;

  /* Authorize peer */
  r = bus_test_polkit (msg,
                       CAP_SYS_ADMIN,
                       "org.gnome.sysprof2.perf-event-open",
                       NULL,
                       UID_INVALID,
                       &challenge,
                       error);
  if (r < 0)
    return r;
  else if (r == 0)
    return -EACCES;

  attr.comm = !!comm;
  attr.config = config;
  attr.disabled = disabled;
  attr.exclude_idle = !!exclude_idle;
  attr.mmap = !!mmap_;
  attr.sample_id_all = sample_id_all;
  attr.sample_period = sample_period;
  attr.sample_type = sample_type;
  attr.task = !!task;
  attr.type = type;
  attr.wakeup_events = wakeup_events;

#ifdef HAVE_PERF_CLOCKID
  if (!use_clockid || clockid < 0)
    attr.clockid = CLOCK_MONOTONIC_RAW;
  else
    attr.clockid = clockid;
  attr.use_clockid = use_clockid;
#endif

  attr.size = sizeof attr;

  fd = _perf_event_open (&attr, pid, cpu, -1, 0);
  if (fd < 0)
    {
      fprintf (stderr,
               "Failed to open perf event stream: %s\n",
               strerror (errno));
      return -EINVAL;
    }

  sd_bus_message_append_basic (reply, SD_BUS_TYPE_UNIX_FD, &fd);
  r = sd_bus_send (NULL, reply, NULL);
  sd_bus_message_unref (reply);

  close (fd);

  return r;
}

static const sd_bus_vtable sysprofd_vtable[] = {
  SD_BUS_VTABLE_START (0),
  SD_BUS_METHOD ("PerfEventOpen", "a{sv}iit", "h", sysprofd_perf_event_open, SD_BUS_VTABLE_UNPRIVILEGED),
  SD_BUS_METHOD ("GetKernelSymbols", "", "a(tys)", sysprofd_get_kernel_symbols, SD_BUS_VTABLE_UNPRIVILEGED),
  SD_BUS_VTABLE_END
};

int
main (int   argc,
      char *argv[])
{
  sd_bus_slot *slot = NULL;
  sd_bus *bus = NULL;
  int r;

  /* Connect to the system bus */
  r = sd_bus_default_system (&bus);
  if (r < 0)
    {
      fprintf (stderr,
               "Failed to connect to system bus: %s\n",
               strerror (-r));
      goto failure;
    }

  /* Install our object */
  r = sd_bus_add_object_vtable (bus,
                                &slot,
                                "/org/gnome/Sysprof2",
                                "org.gnome.Sysprof2",
                                sysprofd_vtable,
                                NULL);
  if (r < 0)
    {
      fprintf (stderr,
               "Failed to install object on bus: %s\n",
               strerror (-r));
      goto failure;
    }

  /* Request our well-known name on the bus */
  r = sd_bus_request_name (bus, "org.gnome.Sysprof2", 0);
  if (r < 0)
    {
      fprintf (stderr,
               "Failed to register name on the bus: %s\n",
               strerror (-r));
      goto failure;
    }

  for (;;)
    {
      /* Process requests */
      r = sd_bus_process (bus, NULL);
      if (r < 0)
        {
          fprintf (stderr,
                   "Failed to process bus: %s\n",
                   strerror (-r));
          goto failure;
        }

      /* If we processed a request, continue processing */
      if (r > 0)
        continue;

      /* Wait for the next request to process */
      r = sd_bus_wait (bus, BUS_TIMEOUT_USEC);
      if (r < 0)
        {
          fprintf (stderr,
                   "Failed to wait on bus: %s\n",
                   strerror (-r));
          goto failure;
        }

      /*
       * If we timed out, we can expire, we will be auto-started by
       * systemd or dbus on the next activation request.
       */
      if (r == 0)
        break;
    }

failure:
  sd_bus_slot_unref (slot);
  sd_bus_unref (bus);

  return r < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
