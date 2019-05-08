/***
  This file is part of systemd.

  Copyright 2013 Lennart Poettering

  systemd is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  systemd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/

#include <assert.h>
#include <errno.h>

#include "sd-bus-helper.h"

/*
 * Various macros to simplify lifing code from sd-bus.
 */
#define assert_return(expr,val) do { if (!(expr)) return (val); } while (0)
#define _cleanup_(f) __attribute__((cleanup(f)))
#define STRV_FOREACH_PAIR(x, y, l) \
  for ((x) = (l), (y) = (x+1); (x) && *(x) && *(y); (x) += 2, (y) = (x + 1))

/*
 * To support systemd 222, we need a couple helpers that were added
 * in 229. If we update code from systemd in the future, we can probably
 * drop these helpres.
 */

static void
_sd_bus_message_unrefp (sd_bus_message **m)
{
  if (m && *m)
    {
      sd_bus_message_unref (*m);
      *m = NULL;
    }
}

static void
_sd_bus_creds_unrefp (sd_bus_creds **c)
{
  if (c && *c)
    {
      sd_bus_creds_unref (*c);
      *c = NULL;
    }
}

/*
 * Begin verbatim code from systemd. Please try to keep this in sync until
 * systemd exposes helpers for polkit integration.
 */
static int
check_good_user (sd_bus_message *m,
                 uid_t           good_user)
{
  _cleanup_(_sd_bus_creds_unrefp) sd_bus_creds *creds = NULL;
  uid_t sender_uid;
  int r;

  assert (m);

  if (good_user == UID_INVALID)
    return 0;

  r = sd_bus_query_sender_creds(m, SD_BUS_CREDS_EUID, &creds);
  if (r < 0)
    return r;

  /* Don't trust augmented credentials for authorization */
  assert_return((sd_bus_creds_get_augmented_mask(creds) & SD_BUS_CREDS_EUID) == 0, -EPERM);

  r = sd_bus_creds_get_euid(creds, &sender_uid);
  if (r < 0)
    return r;

  return sender_uid == good_user;
}

int
bus_test_polkit (sd_bus_message  *call,
                 int              capability,
                 const char      *action,
                 const char     **details,
                 uid_t            good_user,
                 bool            *_challenge,
                 sd_bus_error    *e)
{
  int r;

  assert (call);
  assert (action);

  /* Tests non-interactively! */

  r = check_good_user(call, good_user);
  if (r != 0)
    return r;

  r = sd_bus_query_sender_privilege(call, capability);
  if (r < 0)
    return r;
  else if (r > 0)
    return 1;
  else {
    _cleanup_(_sd_bus_message_unrefp) sd_bus_message *request = NULL;
    _cleanup_(_sd_bus_message_unrefp) sd_bus_message *reply = NULL;
    int authorized = false, challenge = false;
    const char *sender, **k, **v;

    sender = sd_bus_message_get_sender(call);
    if (!sender)
      return -EBADMSG;

    r = sd_bus_message_new_method_call(sd_bus_message_get_bus (call),
                                       &request,
                                       "org.freedesktop.PolicyKit1",
                                       "/org/freedesktop/PolicyKit1/Authority",
                                       "org.freedesktop.PolicyKit1.Authority",
                                       "CheckAuthorization");
    if (r < 0)
      return r;

    r = sd_bus_message_append(request,
                              "(sa{sv})s",
                              "system-bus-name", 1, "name", "s", sender,
                              action);
    if (r < 0)
      return r;

    r = sd_bus_message_open_container(request, 'a', "{ss}");
    if (r < 0)
      return r;

    STRV_FOREACH_PAIR(k, v, details) {
      r = sd_bus_message_append(request, "{ss}", *k, *v);
      if (r < 0)
        return r;
    }

    r = sd_bus_message_close_container(request);
    if (r < 0)
      return r;

    r = sd_bus_message_append(request, "us", 0, NULL);
    if (r < 0)
      return r;

    r = sd_bus_call(sd_bus_message_get_bus(call), request, 0, e, &reply);
    if (r < 0) {
      /* Treat no PK available as access denied */
      if (sd_bus_error_has_name(e, SD_BUS_ERROR_SERVICE_UNKNOWN)) {
        sd_bus_error_free(e);
        return -EACCES;
      }

      return r;
    }

    r = sd_bus_message_enter_container(reply, 'r', "bba{ss}");
    if (r < 0)
      return r;

    r = sd_bus_message_read(reply, "bb", &authorized, &challenge);
    if (r < 0)
      return r;

    if (authorized)
      return 1;

    if (_challenge) {
      *_challenge = challenge;
      return 0;
    }
  }

  return -EACCES;
}
