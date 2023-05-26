/* sysprof-controlfd-instrument.c
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

#include "sysprof-controlfd-instrument-private.h"

struct _SysprofControlfdInstrument
{
  SysprofInstrument parent_instance;
};

G_DEFINE_FINAL_TYPE (SysprofControlfdInstrument, sysprof_controlfd_instrument, SYSPROF_TYPE_INSTRUMENT)

static void
sysprof_controlfd_instrument_finalize (GObject *object)
{
  SysprofControlfdInstrument *self = (SysprofControlfdInstrument *)object;

  G_OBJECT_CLASS (sysprof_controlfd_instrument_parent_class)->finalize (object);
}

static void
sysprof_controlfd_instrument_class_init (SysprofControlfdInstrumentClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_controlfd_instrument_finalize;
}

static void
sysprof_controlfd_instrument_init (SysprofControlfdInstrument *self)
{
}

SysprofInstrument *
_sysprof_controlfd_instrument_new (void)
{
  return g_object_new (SYSPROF_TYPE_CONTROLFD_INSTRUMENT, NULL);
}
