/* sysprof-polkit.c
 *
 * Copyright 2023 Christian Hergert <chergert@redhat.com>
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
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"

#include "sysprof-polkit-private.h"

static void
sysprof_polkit_authority_cb (GObject      *object,
                             GAsyncResult *result,
                             gpointer      user_data)
{
  g_autoptr(PolkitAuthority) authority = NULL;
  g_autoptr(DexPromise) promise = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (DEX_IS_PROMISE (promise));

  if (!(authority = polkit_authority_get_finish (result, &error)))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else
    dex_promise_resolve_object (promise, g_steal_pointer (&authority));
}

static DexFuture *
_sysprof_polkit_authority (void)
{
  DexPromise *promise = dex_promise_new ();

  polkit_authority_get_async (dex_promise_get_cancellable (DEX_PROMISE (promise)),
                              sysprof_polkit_authority_cb,
                              dex_ref (promise));

  return DEX_FUTURE (promise);
}

static void
sysprof_polkit_check_cb (GObject      *object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  PolkitAuthority *authority = (PolkitAuthority *)object;
  g_autoptr(PolkitAuthorizationResult) res = NULL;
  g_autoptr(DexPromise) promise = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (POLKIT_IS_AUTHORITY (authority));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (DEX_IS_PROMISE (promise));

  if (!(res = polkit_authority_check_authorization_finish (authority, result, &error)))
    dex_promise_reject (promise, g_steal_pointer (&error));
  else if (!polkit_authorization_result_get_is_authorized (res))
    dex_promise_reject (promise,
                        g_error_new (G_DBUS_ERROR,
                                     G_DBUS_ERROR_AUTH_FAILED,
                                     "Failed to authorize user credentials"));
  else
    dex_promise_resolve_boolean (promise, TRUE);
}

static DexFuture *
_sysprof_polkit_check (PolkitAuthority               *authority,
                       PolkitSubject                 *subject,
                       const char                    *policy,
                       PolkitDetails                 *details,
                       PolkitCheckAuthorizationFlags  flags)
{
  DexPromise *promise = dex_promise_new ();

  polkit_authority_check_authorization (authority,
                                        subject,
                                        policy,
                                        details,
                                        flags,
                                        dex_promise_get_cancellable (DEX_PROMISE (promise)),
                                        sysprof_polkit_check_cb,
                                        dex_ref (promise));

  return DEX_FUTURE (promise);
}

typedef struct _Authorize
{
  GDBusConnection *connection;
  char            *policy;
  PolkitDetails   *details;
  guint            allow_user_interaction : 1;
} Authorize;

static void
authorize_free (gpointer data)
{
  Authorize *auth = data;

  g_clear_pointer (&auth->policy, g_free);
  g_clear_object (&auth->connection);
  g_clear_object (&auth->details);
  g_free (auth);
}

static Authorize *
authorize_new (GDBusConnection *connection,
               const char      *policy,
               PolkitDetails   *details,
               gboolean         allow_user_interaction)
{
  Authorize *auth;

  auth = g_new0 (Authorize, 1);
  g_set_object (&auth->connection, connection);
  g_set_object (&auth->details, details);
  auth->policy = g_strdup (policy);
  auth->allow_user_interaction = !!allow_user_interaction;

  return auth;
}

static DexFuture *
authorize_fiber (gpointer data)
{
  g_autoptr(PolkitAuthority) authority = NULL;
  g_autoptr(PolkitSubject) subject = NULL;
  g_autoptr(GError) error = NULL;
  const char *bus_name;
  Authorize *auth = data;
  guint flags = 0;

  g_assert (auth != NULL);
  g_assert (G_IS_DBUS_CONNECTION (auth->connection));
  g_assert (!auth->details || POLKIT_IS_DETAILS (auth->details));
  g_assert (auth->policy != NULL);

  bus_name = g_dbus_connection_get_unique_name (auth->connection);
  subject = polkit_system_bus_name_new (bus_name);

  if (!(authority = dex_await_object (_sysprof_polkit_authority (), &error)))
    return dex_future_new_for_error (g_steal_pointer (&error));

  if (auth->allow_user_interaction)
    flags |= POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION;

  if (!dex_await_boolean (_sysprof_polkit_check (authority, subject, auth->policy, auth->details, flags), &error))
    return dex_future_new_for_error (g_steal_pointer (&error));

  return dex_future_new_for_boolean (TRUE);
}

DexFuture *
_sysprof_polkit_authorize (GDBusConnection *connection,
                           const char      *policy,
                           PolkitDetails   *details,
                           gboolean         allow_user_interaction)
{
  g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), NULL);
  g_return_val_if_fail (policy != NULL, NULL);
  g_return_val_if_fail (!details || POLKIT_IS_DETAILS (details), NULL);

  return dex_scheduler_spawn (NULL, 0,
                              authorize_fiber,
                              authorize_new (connection, policy, details, allow_user_interaction),
                              authorize_free);
}
