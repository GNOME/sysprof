/* sysprof-graphics-info.c
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

#include "sysprof-graphics-info.h"
#include "sysprof-device-info-private.h"
#include "sysprof-instrument-private.h"

/**
 * SysprofGraphicsInfo:
 *
 * Collects cached graphics device identity once during preparation.
 * The default mode only reads cached sysfs attributes and hardware database
 * names, without opening device nodes or probing hardware.
 *
 * Enabling [property@GraphicsInfo:detailed] also runs eglinfo and vulkaninfo.
 * These optional tools may wake devices and affect power measurements.
 * Reports are compressed capture attachments available in the file viewer.
 *
 * Since: 52
 */

struct _SysprofGraphicsInfo
{
  SysprofInstrument  parent_instance;

  GCancellable      *cancellable;

  guint              detailed : 1;
};

struct _SysprofGraphicsInfoClass
{
  SysprofInstrumentClass parent_class;
};

enum
{
  PROP_0,
  PROP_DETAILED,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofGraphicsInfo, sysprof_graphics_info, SYSPROF_TYPE_INSTRUMENT)

static GParamSpec *properties[N_PROPS];

static DexFuture *
sysprof_graphics_info_prepare (SysprofInstrument *instrument,
                               SysprofRecording  *recording)
{
  SysprofGraphicsInfo *self = (SysprofGraphicsInfo *)instrument;

  g_assert (SYSPROF_IS_GRAPHICS_INFO (self));
  g_assert (SYSPROF_IS_RECORDING (recording));

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);
  self->cancellable = g_cancellable_new ();

  return _sysprof_device_info_prepare (recording, TRUE,
                                      self->detailed, self->cancellable);
}

static void
sysprof_graphics_info_dispose (GObject *object)
{
  SysprofGraphicsInfo *self = (SysprofGraphicsInfo *)object;

  if (self->cancellable != NULL)
    g_cancellable_cancel (self->cancellable);

  g_clear_object (&self->cancellable);

  G_OBJECT_CLASS (sysprof_graphics_info_parent_class)->dispose (object);
}

static void
sysprof_graphics_info_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  SysprofGraphicsInfo *self = (SysprofGraphicsInfo *)object;

  switch (prop_id)
    {
    case PROP_DETAILED:
      g_value_set_boolean (value, sysprof_graphics_info_get_detailed (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_graphics_info_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  SysprofGraphicsInfo *self = (SysprofGraphicsInfo *)object;

  switch (prop_id)
    {
    case PROP_DETAILED:
      sysprof_graphics_info_set_detailed (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_graphics_info_class_init (SysprofGraphicsInfoClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  object_class->dispose = sysprof_graphics_info_dispose;
  object_class->get_property = sysprof_graphics_info_get_property;
  object_class->set_property = sysprof_graphics_info_set_property;

  instrument_class->prepare = sysprof_graphics_info_prepare;

  /**
   * SysprofGraphicsInfo:detailed:
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
sysprof_graphics_info_init (SysprofGraphicsInfo *self)
{
  self->cancellable = g_cancellable_new ();
}

/**
 * sysprof_graphics_info_new:
 *
 * Creates an instrument that collects cached graphics device identity.
 *
 * Returns: (transfer full): a new #SysprofGraphicsInfo
 * Since: 52
 */
SysprofInstrument *
sysprof_graphics_info_new (void)
{
  return g_object_new (SYSPROF_TYPE_GRAPHICS_INFO, NULL);
}

/**
 * sysprof_graphics_info_get_detailed:
 * @self: a #SysprofGraphicsInfo
 *
 * Gets whether optional diagnostic tools will run during preparation.
 *
 * Returns: %TRUE if detailed collection is enabled
 * Since: 52
 */
gboolean
sysprof_graphics_info_get_detailed (SysprofGraphicsInfo *self)
{
  g_return_val_if_fail (SYSPROF_IS_GRAPHICS_INFO (self), FALSE);

  return self->detailed;
}

/**
 * sysprof_graphics_info_set_detailed:
 * @self: a #SysprofGraphicsInfo
 * @detailed: whether to enable detailed collection
 *
 * Sets whether to run eglinfo and vulkaninfo during preparation. These tools
 * may wake devices and affect power measurements. Set this before recording.
 *
 * Since: 52
 */
void
sysprof_graphics_info_set_detailed (SysprofGraphicsInfo *self,
                                    gboolean             detailed)
{
  g_return_if_fail (SYSPROF_IS_GRAPHICS_INFO (self));

  detailed = !!detailed;

  if (self->detailed != detailed)
    {
      self->detailed = detailed;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DETAILED]);
    }
}
