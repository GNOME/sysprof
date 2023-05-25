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

G_DEFINE_ABSTRACT_TYPE (SysprofInstrument, sysprof_instrument, G_TYPE_OBJECT)

static char **
sysprof_instrument_real_list_required_policy (SysprofInstrument *self)
{
  return NULL;
}

static void
sysprof_instrument_class_init (SysprofInstrumentClass *klass)
{
  klass->list_required_policy = sysprof_instrument_real_list_required_policy;
}

static void
sysprof_instrument_init (SysprofInstrument *self)
{
}

char **
_sysprof_instrument_list_required_policy (SysprofInstrument *self)
{
  g_return_val_if_fail (SYSPROF_IS_INSTRUMENT (self), NULL);

  return SYSPROF_INSTRUMENT_GET_CLASS (self)->list_required_policy (self);
}
