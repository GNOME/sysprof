/* sysprof-power-profile.c
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

#include <glib/gi18n.h>

#include "sysprof-power-profile.h"
#include "sysprof-instrument-private.h"
#include "sysprof-recording-private.h"

struct _SysprofPowerProfile
{
  SysprofInstrument parent_instance;
  SysprofRecording *recording;
  char *restore_id;
  char *id;
};

struct _SysprofPowerProfileClass
{
  SysprofInstrumentClass parent_class;
};

G_DEFINE_FINAL_TYPE (SysprofPowerProfile, sysprof_power_profile, SYSPROF_TYPE_INSTRUMENT)

enum {
  PROP_0,
  PROP_ID,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static char **
sysprof_power_profile_list_required_policy (SysprofInstrument *instrument)
{
  static const char *required_policy[] = {"org.freedesktop.UPower.PowerProfiles.switch-profile", NULL};

  return g_strdupv ((char **)required_policy);
}

static void
restore_power_profile (char *power_profile)
{
  g_debug ("Restoring performance profile to %s\n", power_profile);

  if (power_profile != NULL)
    {
      g_autofree char *hold = power_profile;
      g_autoptr(GDBusConnection) bus = NULL;
      g_autoptr(GVariant) ret = NULL;
      g_autoptr(GError) error = NULL;

      if (!(bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL)))
        return;

      ret = g_dbus_connection_call_sync (bus,
                                         "org.freedesktop.UPower.PowerProfiles",
                                         "/org/freedesktop/UPower/PowerProfiles",
                                         "org.freedesktop.DBus.Properties",
                                         "Set",
                                         g_variant_new ("(ssv)",
                                                        "org.freedesktop.UPower.PowerProfiles",
                                                        "ActiveProfile",
                                                        g_variant_new_string (power_profile)),
                                         NULL,
                                         G_DBUS_CALL_FLAGS_NONE,
                                         -1,
                                         NULL, &error);

      if (error != NULL)
        g_warning ("Failed to restore performance profile: %s", error->message);
    }
}

static DexFuture *
sysprof_power_profile_release_cb (DexFuture *future,
                                  gpointer   user_data)
{
  SysprofPowerProfile *self = user_data;

  g_assert (DEX_IS_FUTURE (future));
  g_assert (SYSPROF_IS_POWER_PROFILE (self));

  g_clear_pointer (&self->restore_id, restore_power_profile);

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
sysprof_power_profile_record (SysprofInstrument *instrument,
                              SysprofRecording  *recording,
                              GCancellable      *cancellable)
{
  g_assert (SYSPROF_IS_POWER_PROFILE (instrument));
  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (G_IS_CANCELLABLE (cancellable));

  return dex_future_finally (dex_cancellable_new_from_cancellable (cancellable),
                             sysprof_power_profile_release_cb,
                             g_object_ref (instrument),
                             g_object_unref);
}

static char *
get_string_prop (GVariant *v)
{
  g_autoptr(GVariant) child1 = g_variant_get_child_value (v, 0);
  g_autoptr(GVariant) child2 = g_variant_get_child_value (child1, 0);

  return g_variant_dup_string (child2, NULL);
}

static DexFuture *
sysprof_power_profile_prepare_fiber (gpointer user_data)
{
  SysprofPowerProfile *self = user_data;
  g_autoptr(GDBusConnection) bus = NULL;
  g_autoptr(GVariant) ret = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (SYSPROF_IS_POWER_PROFILE (self));
  g_assert (SYSPROF_IS_RECORDING (self->recording));

  if (self->id == NULL)
    goto failure;

  if (!(bus = dex_await_object (dex_bus_get (G_BUS_TYPE_SYSTEM), &error)))
    goto failure;

  if (!(ret = dex_await_variant (dex_dbus_connection_call (bus,
                                                           "org.freedesktop.UPower.PowerProfiles",
                                                           "/org/freedesktop/UPower/PowerProfiles",
                                                           "org.freedesktop.DBus.Properties",
                                                           "Get",
                                                           g_variant_new ("(ss)",
                                                                          "org.freedesktop.UPower.PowerProfiles",
                                                                          "ActiveProfile"),
                                                           G_VARIANT_TYPE ("(v)"),
                                                           G_DBUS_CALL_FLAGS_NONE,
                                                           -1), &error)))
    goto failure;

  /* Save the value to be restored after recording */
  g_clear_pointer (&self->restore_id, g_free);
  self->restore_id = get_string_prop (ret);
  g_clear_pointer (&ret, g_variant_unref);

  /* We don't use "HoldPorfile" here because it is not reliable */
  if (g_strcmp0 (self->id, self->restore_id) != 0)
    {
      if (!(ret = dex_await_variant (dex_dbus_connection_call (bus,
                                                               "org.freedesktop.UPower.PowerProfiles",
                                                               "/org/freedesktop/UPower/PowerProfiles",
                                                               "org.freedesktop.DBus.Properties",
                                                               "Set",
                                                               g_variant_new ("(ssv)",
                                                                              "org.freedesktop.UPower.PowerProfiles",
                                                                              "ActiveProfile",
                                                                              g_variant_new_string (self->id)),
                                                               NULL,
                                                               G_DBUS_CALL_FLAGS_NONE,
                                                               -1), &error)))
        goto failure;

      _sysprof_recording_diagnostic (self->recording,
                                     "Power Profile",
                                     "Power profile temporarily set to %s from %s",
                                     self->id, self->restore_id);
    }
  else
    {
      g_clear_pointer (&self->restore_id, g_free);
    }

failure:
  if (error != NULL)
    _sysprof_recording_diagnostic (self->recording,
                                   "Power Profile",
                                   "Failed to set power profile to “%s”: %s",
                                   self->id, error->message);

  g_clear_object (&self->recording);

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
sysprof_power_profile_prepare (SysprofInstrument *instrument,
                               SysprofRecording  *recording)
{
  SysprofPowerProfile *self = (SysprofPowerProfile *)instrument;

  g_assert (SYSPROF_IS_POWER_PROFILE (self));
  g_assert (SYSPROF_IS_RECORDING (recording));

  g_set_object (&self->recording, recording);

  return dex_scheduler_spawn (NULL, 0,
                              sysprof_power_profile_prepare_fiber,
                              g_object_ref (self),
                              g_object_unref);
}

static void
sysprof_power_profile_dispose (GObject *object)
{
  SysprofPowerProfile *self = (SysprofPowerProfile *)object;

  g_clear_pointer (&self->id, g_free);
  g_clear_pointer (&self->restore_id, g_free);

  G_OBJECT_CLASS (sysprof_power_profile_parent_class)->dispose (object);
}

static void
sysprof_power_profile_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  SysprofPowerProfile *self = SYSPROF_POWER_PROFILE (object);

  switch (prop_id)
    {
    case PROP_ID:
      g_value_set_string (value, sysprof_power_profile_get_id (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_power_profile_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  SysprofPowerProfile *self = SYSPROF_POWER_PROFILE (object);

  switch (prop_id)
    {
    case PROP_ID:
      self->id = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_power_profile_class_init (SysprofPowerProfileClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  object_class->dispose = sysprof_power_profile_dispose;
  object_class->get_property = sysprof_power_profile_get_property;
  object_class->set_property = sysprof_power_profile_set_property;

  instrument_class->list_required_policy = sysprof_power_profile_list_required_policy;
  instrument_class->prepare = sysprof_power_profile_prepare;
  instrument_class->record = sysprof_power_profile_record;

  properties[PROP_ID] =
    g_param_spec_string ("id", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_power_profile_init (SysprofPowerProfile *self)
{
}

SysprofInstrument *
sysprof_power_profile_new (const char *id)
{
  return g_object_new (SYSPROF_TYPE_POWER_PROFILE,
                       "id", id,
                       NULL);
}

const char *
sysprof_power_profile_get_id (SysprofPowerProfile *self)
{
  g_return_val_if_fail (SYSPROF_IS_POWER_PROFILE (self), NULL);

  return self->id;
}
