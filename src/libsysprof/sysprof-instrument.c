/* sysprof-instrument.c
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

#include "sysprof-instrument-private.h"

#if HAVE_POLKIT
# include "sysprof-polkit-private.h"
#endif

G_DEFINE_ABSTRACT_TYPE (SysprofInstrument, sysprof_instrument, G_TYPE_OBJECT)

static char **
sysprof_instrument_real_list_required_policy (SysprofInstrument *instrument)
{
  g_assert (SYSPROF_IS_INSTRUMENT (instrument));

  return NULL;
}

static DexFuture *
sysprof_instrument_real_prepare (SysprofInstrument *instrument,
                                 SysprofRecording  *recording)
{
  g_assert (SYSPROF_IS_INSTRUMENT (instrument));
  g_assert (SYSPROF_IS_RECORDING (recording));

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
sysprof_instrument_real_record (SysprofInstrument *instrument,
                                SysprofRecording  *recording,
                                GCancellable      *cancellable)
{
  g_assert (SYSPROF_IS_INSTRUMENT (instrument));
  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  return dex_future_new_for_boolean (TRUE);
}

static void
sysprof_instrument_class_init (SysprofInstrumentClass *klass)
{
  klass->list_required_policy = sysprof_instrument_real_list_required_policy;
  klass->prepare = sysprof_instrument_real_prepare;
  klass->record = sysprof_instrument_real_record;
}

static void
sysprof_instrument_init (SysprofInstrument *self)
{
}

static char **
_sysprof_instrument_list_required_policy (SysprofInstrument *self)
{
  g_assert (SYSPROF_IS_INSTRUMENT (self));

  if (SYSPROF_INSTRUMENT_GET_CLASS (self)->list_required_policy)
    return SYSPROF_INSTRUMENT_GET_CLASS (self)->list_required_policy (self);

  return NULL;
}

static DexFuture *
_sysprof_instrument_prepare (SysprofInstrument *self,
                             SysprofRecording  *recording)
{
  g_assert (SYSPROF_IS_INSTRUMENT (self));
  g_assert (SYSPROF_IS_RECORDING (recording));

  if (SYSPROF_INSTRUMENT_GET_CLASS (self)->prepare)
    return SYSPROF_INSTRUMENT_GET_CLASS (self)->prepare (self, recording);

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
_sysprof_instrument_record (SysprofInstrument *self,
                            SysprofRecording  *recording,
                            GCancellable      *cancellable)
{
  g_assert (SYSPROF_IS_INSTRUMENT (self));
  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (G_IS_CANCELLABLE (cancellable));

  if (SYSPROF_INSTRUMENT_GET_CLASS (self)->record)
    return SYSPROF_INSTRUMENT_GET_CLASS (self)->record (self, recording, cancellable);

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
_sysprof_instrument_augment (SysprofInstrument *self,
                             SysprofRecording  *recording)
{
  g_assert (SYSPROF_IS_INSTRUMENT (self));
  g_assert (SYSPROF_IS_RECORDING (recording));

  if (SYSPROF_INSTRUMENT_GET_CLASS (self)->augment)
    return SYSPROF_INSTRUMENT_GET_CLASS (self)->augment (self, recording);

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
_sysprof_instrument_process_started (SysprofInstrument *self,
                                     SysprofRecording  *recording,
                                     int                pid,
                                     const char        *comm)
{
  g_assert (SYSPROF_IS_INSTRUMENT (self));
  g_assert (SYSPROF_IS_RECORDING (recording));

  if (SYSPROF_INSTRUMENT_GET_CLASS (self)->process_started)
    return SYSPROF_INSTRUMENT_GET_CLASS (self)->process_started (self, recording, pid, comm);

  return dex_future_new_for_boolean (TRUE);
}

static char **
_sysprof_instruments_list_required_policy (GPtrArray *instruments)
{
  g_autoptr(GPtrArray) all_policy = NULL;

  g_return_val_if_fail (instruments != NULL, NULL);

  all_policy = g_ptr_array_new_null_terminated (0, g_free, TRUE);

  for (guint i = 0; i < instruments->len; i++)
    {
      SysprofInstrument *instrument = g_ptr_array_index (instruments, i);
      g_auto(GStrv) policy = _sysprof_instrument_list_required_policy (instrument);

      if (policy == NULL || policy[0] == NULL)
        continue;

      for (guint j = 0; policy[j]; j++)
        {
          gboolean found = FALSE;

          for (guint k = 0; !found && k < all_policy->len; k++)
            found = strcmp (policy[j], g_ptr_array_index (all_policy, k)) == 0;

          if (!found)
            g_ptr_array_add (all_policy, g_strdup (policy[j]));
        }
    }

  if (all_policy->len == 0)
    return NULL;

  return (char **)g_ptr_array_free (g_steal_pointer (&all_policy), FALSE);
}

DexFuture *
_sysprof_instruments_acquire_policy (GPtrArray        *instruments,
                                     SysprofRecording *recording)
{
  g_autoptr(GDBusConnection) connection = NULL;
  g_autoptr(GError) error = NULL;
  g_auto(GStrv) required_policy = NULL;

  g_return_val_if_fail (instruments != NULL, NULL);
  g_return_val_if_fail (SYSPROF_IS_RECORDING (recording), NULL);

  /* Ensure we have access to the System D-Bus so that we can get
   * access to sysprofd for system information.
   */
  if (!(connection = dex_await_object (dex_bus_get (G_BUS_TYPE_SYSTEM), &error)))
    return dex_future_new_for_error (g_steal_pointer (&error));

  /* First ensure that all our required policy have been acquired on
   * the bus so that we don't need to individually acquire them from
   * each of the instruments.
   */
  if ((required_policy = _sysprof_instruments_list_required_policy (instruments)))
    {
#if HAVE_POLKIT
      for (guint i = 0; required_policy[i]; i++)
        {
          if (!dex_await_boolean (_sysprof_polkit_authorize (connection,
                                                             required_policy[i],
                                                             NULL,
                                                             TRUE), &error))
            return dex_future_new_for_error (g_steal_pointer (&error));
        }
#endif
    }

  return dex_future_new_for_boolean (TRUE);
}

DexFuture *
_sysprof_instruments_prepare (GPtrArray        *instruments,
                              SysprofRecording *recording)
{
  g_autoptr(GPtrArray) futures = NULL;

  g_return_val_if_fail (instruments != NULL, NULL);
  g_return_val_if_fail (SYSPROF_IS_RECORDING (recording), NULL);

  futures = g_ptr_array_new_with_free_func (dex_unref);

  for (guint i = 0; i < instruments->len; i++)
    {
      SysprofInstrument *instrument = g_ptr_array_index (instruments, i);

      g_ptr_array_add (futures, _sysprof_instrument_prepare (instrument, recording));
    }

  if (futures->len == 0)
    return dex_future_new_for_boolean (TRUE);

  return dex_future_allv ((DexFuture **)futures->pdata, futures->len);
}

DexFuture *
_sysprof_instruments_record (GPtrArray        *instruments,
                             SysprofRecording *recording,
                             GCancellable     *cancellable)
{
  g_autoptr(GPtrArray) futures = NULL;

  g_return_val_if_fail (instruments != NULL, NULL);
  g_return_val_if_fail (SYSPROF_IS_RECORDING (recording), NULL);
  g_return_val_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable), NULL);

  futures = g_ptr_array_new_with_free_func (dex_unref);

  for (guint i = 0; i < instruments->len; i++)
    {
      SysprofInstrument *instrument = g_ptr_array_index (instruments, i);

      g_ptr_array_add (futures, _sysprof_instrument_record (instrument, recording, cancellable));
    }

  if (futures->len == 0)
    return dex_future_new_for_boolean (TRUE);

  return dex_future_allv ((DexFuture **)futures->pdata, futures->len);
}

DexFuture *
_sysprof_instruments_augment (GPtrArray        *instruments,
                              SysprofRecording *recording)
{
  g_autoptr(GPtrArray) futures = NULL;

  g_return_val_if_fail (instruments != NULL, NULL);
  g_return_val_if_fail (SYSPROF_IS_RECORDING (recording), NULL);

  futures = g_ptr_array_new_with_free_func (dex_unref);

  for (guint i = 0; i < instruments->len; i++)
    {
      SysprofInstrument *instrument = g_ptr_array_index (instruments, i);

      g_ptr_array_add (futures, _sysprof_instrument_augment (instrument, recording));
    }

  if (futures->len == 0)
    return dex_future_new_for_boolean (TRUE);

  return dex_future_allv ((DexFuture **)futures->pdata, futures->len);
}

DexFuture *
_sysprof_instruments_process_started (GPtrArray        *instruments,
                                      SysprofRecording *recording,
                                      int               pid,
                                      const char       *comm)
{
  g_autoptr(GPtrArray) futures = NULL;

  g_return_val_if_fail (instruments != NULL, NULL);
  g_return_val_if_fail (SYSPROF_IS_RECORDING (recording), NULL);

  futures = g_ptr_array_new_with_free_func (dex_unref);

  for (guint i = 0; i < instruments->len; i++)
    {
      SysprofInstrument *instrument = g_ptr_array_index (instruments, i);

      g_ptr_array_add (futures, _sysprof_instrument_process_started (instrument, recording, pid, comm));
    }

  if (futures->len == 0)
    return dex_future_new_for_boolean (TRUE);

  return dex_future_allv ((DexFuture **)futures->pdata, futures->len);
}

void
_sysprof_instruments_set_connection (GPtrArray       *instruments,
                                     GDBusConnection *connection)
{
  g_return_if_fail (instruments != NULL);
  g_return_if_fail (!connection || G_IS_DBUS_CONNECTION (connection));

  for (guint i = 0; i < instruments->len; i++)
    {
      SysprofInstrument *instrument = g_ptr_array_index (instruments, i);

      if (SYSPROF_INSTRUMENT_GET_CLASS (instrument)->set_connection)
        SYSPROF_INSTRUMENT_GET_CLASS (instrument)->set_connection (instrument, connection);
    }
}
