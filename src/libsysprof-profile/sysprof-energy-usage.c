/* sysprof-energy-usage.c
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

#include "sysprof-proxied-instrument-private.h"
#include "sysprof-energy-usage.h"

struct _SysprofEnergyUsage
{
  SysprofProxiedInstrument parent_instance;
};

struct _SysprofEnergyUsageClass
{
  SysprofProxiedInstrumentClass parent_class;
};

G_DEFINE_FINAL_TYPE (SysprofEnergyUsage, sysprof_energy_usage, SYSPROF_TYPE_PROXIED_INSTRUMENT)

static void
sysprof_energy_usage_class_init (SysprofEnergyUsageClass *klass)
{
}

static void
sysprof_energy_usage_init (SysprofEnergyUsage *self)
{
  SYSPROF_PROXIED_INSTRUMENT (self)->call_stop_first = TRUE;
}

SysprofInstrument *
sysprof_energy_usage_new (void)
{
  return g_object_new (SYSPROF_TYPE_ENERGY_USAGE,
                       "bus-type", G_BUS_TYPE_SYSTEM,
                       "bus-name", "org.gnome.Sysprof3",
                       "object-path", "/org/gnome/Sysprof3/RAPL",
                       NULL);
}
