/* sysprof-hardware-info.c
 *
 * Copyright 2026 Christian Hergert
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

#include "sysprof-hardware-info.h"
#include "sysprof-device-info-private.h"
#include "sysprof-instrument-private.h"

/**
 * SysprofHardwareInfo:
 *
 * Collects cached PCI and USB device identity once during preparation.
 * The default mode only reads cached sysfs attributes and hardware database
 * names, without opening device nodes or probing hardware.
 *
 * Enabling [property@HardwareInfo:detailed] also runs lspci -v and lsusb -v.
 * These optional tools may wake devices and affect power measurements.
 * Reports are compressed capture attachments available in the file viewer.
 *
 * Since: 52
 */

struct _SysprofHardwareInfo
{
  SysprofInstrument  parent_instance;

  GCancellable      *cancellable;

  guint              detailed : 1;
};

struct _SysprofHardwareInfoClass
{
  SysprofInstrumentClass parent_class;
};

enum
{
  PROP_0,
  PROP_DETAILED,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofHardwareInfo, sysprof_hardware_info, SYSPROF_TYPE_INSTRUMENT)

static GParamSpec *properties[N_PROPS];

static DexFuture *
sysprof_hardware_info_prepare (SysprofInstrument *instrument,
                               SysprofRecording  *recording)
{
  SysprofHardwareInfo *self = (SysprofHardwareInfo *)instrument;

  g_assert (SYSPROF_IS_HARDWARE_INFO (self));
  g_assert (SYSPROF_IS_RECORDING (recording));

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);
  self->cancellable = g_cancellable_new ();

  return _sysprof_device_info_prepare (recording, FALSE,
                                      self->detailed, self->cancellable);
}

static void
sysprof_hardware_info_dispose (GObject *object)
{
  SysprofHardwareInfo *self = (SysprofHardwareInfo *)object;

  if (self->cancellable != NULL)
    g_cancellable_cancel (self->cancellable);

  g_clear_object (&self->cancellable);

  G_OBJECT_CLASS (sysprof_hardware_info_parent_class)->dispose (object);
}

static void
sysprof_hardware_info_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  SysprofHardwareInfo *self = (SysprofHardwareInfo *)object;

  switch (prop_id)
    {
    case PROP_DETAILED:
      g_value_set_boolean (value, sysprof_hardware_info_get_detailed (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_hardware_info_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  SysprofHardwareInfo *self = (SysprofHardwareInfo *)object;

  switch (prop_id)
    {
    case PROP_DETAILED:
      sysprof_hardware_info_set_detailed (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_hardware_info_class_init (SysprofHardwareInfoClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  object_class->dispose = sysprof_hardware_info_dispose;
  object_class->get_property = sysprof_hardware_info_get_property;
  object_class->set_property = sysprof_hardware_info_set_property;

  instrument_class->prepare = sysprof_hardware_info_prepare;

  /**
   * SysprofHardwareInfo:detailed:
   *
   * Whether to run optional diagnostic tools, which may wake devices and
   * affect power measurements. Defaults to %FALSE.
   *
   * Since: 52
   */
  properties[PROP_DETAILED] =
    g_param_spec_boolean ("detailed", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_hardware_info_init (SysprofHardwareInfo *self)
{
  self->cancellable = g_cancellable_new ();
}

/**
 * sysprof_hardware_info_new:
 *
 * Creates an instrument that collects cached PCI and USB device identity.
 *
 * Returns: (transfer full): a new #SysprofHardwareInfo
 * Since: 52
 */
SysprofInstrument *
sysprof_hardware_info_new (void)
{
  return g_object_new (SYSPROF_TYPE_HARDWARE_INFO, NULL);
}

/**
 * sysprof_hardware_info_get_detailed:
 * @self: a #SysprofHardwareInfo
 *
 * Gets whether optional diagnostic tools will run during preparation.
 *
 * Returns: %TRUE if detailed collection is enabled
 * Since: 52
 */
gboolean
sysprof_hardware_info_get_detailed (SysprofHardwareInfo *self)
{
  g_return_val_if_fail (SYSPROF_IS_HARDWARE_INFO (self), FALSE);

  return self->detailed;
}

/**
 * sysprof_hardware_info_set_detailed:
 * @self: a #SysprofHardwareInfo
 * @detailed: whether to enable detailed collection
 *
 * Sets whether to run lspci -v and lsusb -v during preparation. These tools
 * may wake devices and affect power measurements. Set this before recording.
 *
 * Since: 52
 */
void
sysprof_hardware_info_set_detailed (SysprofHardwareInfo *self,
                                    gboolean             detailed)
{
  g_return_if_fail (SYSPROF_IS_HARDWARE_INFO (self));

  detailed = !!detailed;

  if (self->detailed != detailed)
    {
      self->detailed = detailed;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DETAILED]);
    }
}
