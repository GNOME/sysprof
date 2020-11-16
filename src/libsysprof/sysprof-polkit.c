/* sysprof-polkit.c
 *
 * Copyright 2019 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sysprof-polkit"

#include "config.h"

#if HAVE_POLKIT
# include <polkit/polkit.h>
#endif

#include "sysprof-polkit-private.h"
#include "sysprof-backport-autocleanups.h"

#if HAVE_POLKIT
typedef struct
{
  const gchar   *policy;
  PolkitSubject *subject;
  GHashTable    *details;
  guint          allow_user_interaction : 1;
} Authorize;

static void
authorize_free (gpointer data)
{
  Authorize *auth = data;

  g_clear_object (&auth->subject);
  g_clear_pointer (&auth->details, g_hash_table_unref);
  g_slice_free (Authorize, auth);
}

static void
sysprof_polkit_check_authorization_cb (GObject      *object,
                                       GAsyncResult *result,
                                       gpointer      user_data)
{
  PolkitAuthority *authority = (PolkitAuthority *)object;
  g_autoptr(PolkitAuthorizationResult) res = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;

  g_assert (POLKIT_IS_AUTHORITY (authority));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (!(res = polkit_authority_check_authorization_finish (authority, result, &error)))
    g_task_return_error (task, g_steal_pointer (&error));
  else if (!polkit_authorization_result_get_is_authorized (res))
    g_task_return_new_error (task,
                             G_IO_ERROR,
                             G_IO_ERROR_PROXY_AUTH_FAILED,
                             "Failed to authorize user credentials");
  else
    g_task_return_boolean (task, TRUE);
}

static void
sysprof_polkit_get_authority_cb (GObject      *object,
                                 GAsyncResult *result,
                                 gpointer      user_data)
{
  g_autoptr(PolkitAuthority) authority = NULL;
  g_autoptr(PolkitDetails) details = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;
  GCancellable *cancellable;
  Authorize *auth;
  guint flags = 0;

  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  cancellable = g_task_get_cancellable (task);
  auth = g_task_get_task_data (task);

  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));
  g_assert (auth != NULL);
  g_assert (POLKIT_IS_SUBJECT (auth->subject));

  if (!(authority = polkit_authority_get_finish (result, &error)))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  if (auth->allow_user_interaction)
    flags |= POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION;

  if (auth->details != NULL)
    {
      GHashTableIter iter;
      gpointer k, v;

      details = polkit_details_new ();
      g_hash_table_iter_init (&iter, auth->details);
      while (g_hash_table_iter_next (&iter, &k, &v))
        polkit_details_insert (details, k, v);
    }

  polkit_authority_check_authorization (authority,
                                        auth->subject,
                                        auth->policy,
                                        details,
                                        flags,
                                        cancellable,
                                        sysprof_polkit_check_authorization_cb,
                                        g_steal_pointer (&task));
}
#endif

void
_sysprof_polkit_authorize_for_bus_async (GDBusConnection     *bus,
                                         const gchar         *policy,
                                         GHashTable          *details,
                                         gboolean             allow_user_interaction,
                                         GCancellable        *cancellable,
                                         GAsyncReadyCallback  callback,
                                         gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
#if HAVE_POLKIT
  const gchar *bus_name;
  Authorize *auth;
#endif

  g_return_if_fail (G_IS_DBUS_CONNECTION (bus));
  g_return_if_fail (policy != NULL);
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (NULL, cancellable, callback, user_data);
  g_task_set_source_tag (task, _sysprof_polkit_authorize_for_bus_async);

#if HAVE_POLKIT
  bus_name = g_dbus_connection_get_unique_name (bus);

  auth = g_slice_new0 (Authorize);
  auth->subject = polkit_system_bus_name_new (bus_name);
  auth->policy = g_intern_string (policy);
  auth->details = details ? g_hash_table_ref (details) : NULL;
  auth->allow_user_interaction = !!allow_user_interaction;
  g_task_set_task_data (task, auth, authorize_free);

  polkit_authority_get_async (cancellable,
                              sysprof_polkit_get_authority_cb,
                              g_steal_pointer (&task));
#else
  g_task_return_boolean (task, TRUE);
#endif
}

gboolean
_sysprof_polkit_authorize_for_bus_finish (GAsyncResult  *result,
                                          GError       **error)
{
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}
