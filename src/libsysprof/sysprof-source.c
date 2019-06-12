/* sysprof-source.c
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
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

#include "sysprof-source.h"

G_DEFINE_INTERFACE (SysprofSource, sysprof_source, G_TYPE_OBJECT)

enum {
  FAILED,
  FINISHED,
  READY,
  N_SIGNALS
};

static guint signals [N_SIGNALS];

static void
sysprof_source_default_init (SysprofSourceInterface *iface)
{
  signals [FAILED] = g_signal_new ("failed",
                                   G_TYPE_FROM_INTERFACE (iface),
                                   G_SIGNAL_RUN_LAST,
                                   0,
                                   NULL, NULL, NULL,
                                   G_TYPE_NONE, 1, G_TYPE_ERROR);

  signals [FINISHED] = g_signal_new ("finished",
                                     G_TYPE_FROM_INTERFACE (iface),
                                     G_SIGNAL_RUN_LAST,
                                     0, NULL, NULL, NULL, G_TYPE_NONE, 0);

  signals [READY] = g_signal_new ("ready",
                                  G_TYPE_FROM_INTERFACE (iface),
                                  G_SIGNAL_RUN_LAST,
                                  0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

void
sysprof_source_add_pid (SysprofSource *self,
                        GPid      pid)
{
  g_return_if_fail (SYSPROF_IS_SOURCE (self));
  g_return_if_fail (pid != FALSE);

  if (SYSPROF_SOURCE_GET_IFACE (self)->add_pid)
    SYSPROF_SOURCE_GET_IFACE (self)->add_pid (self, pid);
}

void
sysprof_source_emit_finished (SysprofSource *self)
{
  g_return_if_fail (SYSPROF_IS_SOURCE (self));

  g_signal_emit (self, signals [FINISHED], 0);
}

void
sysprof_source_emit_failed (SysprofSource     *self,
                            const GError *error)
{
  g_return_if_fail (SYSPROF_IS_SOURCE (self));
  g_return_if_fail (error != NULL);

  g_signal_emit (self, signals [FAILED], 0, error);
}

void
sysprof_source_emit_ready (SysprofSource *self)
{
  g_return_if_fail (SYSPROF_IS_SOURCE (self));

  g_signal_emit (self, signals [READY], 0);
}

gboolean
sysprof_source_get_is_ready (SysprofSource *self)
{
  g_return_val_if_fail (SYSPROF_IS_SOURCE (self), FALSE);

  if (SYSPROF_SOURCE_GET_IFACE (self)->get_is_ready)
    return SYSPROF_SOURCE_GET_IFACE (self)->get_is_ready (self);

  return TRUE;
}

void
sysprof_source_prepare (SysprofSource *self)
{
  g_return_if_fail (SYSPROF_IS_SOURCE (self));

  if (SYSPROF_SOURCE_GET_IFACE (self)->prepare)
    SYSPROF_SOURCE_GET_IFACE (self)->prepare (self);
}

void
sysprof_source_set_writer (SysprofSource        *self,
                           SysprofCaptureWriter *writer)
{
  g_return_if_fail (SYSPROF_IS_SOURCE (self));
  g_return_if_fail (writer != NULL);

  if (SYSPROF_SOURCE_GET_IFACE (self)->set_writer)
    SYSPROF_SOURCE_GET_IFACE (self)->set_writer (self, writer);
}

void
sysprof_source_start (SysprofSource *self)
{
  g_return_if_fail (SYSPROF_IS_SOURCE (self));

  if (SYSPROF_SOURCE_GET_IFACE (self)->start)
    SYSPROF_SOURCE_GET_IFACE (self)->start (self);
}

void
sysprof_source_stop (SysprofSource *self)
{
  g_return_if_fail (SYSPROF_IS_SOURCE (self));

  if (SYSPROF_SOURCE_GET_IFACE (self)->stop)
    SYSPROF_SOURCE_GET_IFACE (self)->stop (self);
}

void
sysprof_source_modify_spawn (SysprofSource    *self,
                             SysprofSpawnable *spawnable)
{
  g_return_if_fail (SYSPROF_IS_SOURCE (self));
  g_return_if_fail (SYSPROF_IS_SPAWNABLE (spawnable));

  if (SYSPROF_SOURCE_GET_IFACE (self)->modify_spawn)
    SYSPROF_SOURCE_GET_IFACE (self)->modify_spawn (self, spawnable);
}

void
sysprof_source_serialize (SysprofSource *self,
                          GKeyFile      *keyfile,
                          const gchar   *group)
{
  g_return_if_fail (SYSPROF_IS_SOURCE (self));
  g_return_if_fail (keyfile != NULL);
  g_return_if_fail (group != NULL);

  if (SYSPROF_SOURCE_GET_IFACE (self)->serialize)
    SYSPROF_SOURCE_GET_IFACE (self)->serialize (self, keyfile, group);
}

void
sysprof_source_deserialize (SysprofSource *self,
                            GKeyFile      *keyfile,
                            const gchar   *group)
{
  g_return_if_fail (SYSPROF_IS_SOURCE (self));
  g_return_if_fail (keyfile != NULL);
  g_return_if_fail (group != NULL);

  if (SYSPROF_SOURCE_GET_IFACE (self)->deserialize)
    SYSPROF_SOURCE_GET_IFACE (self)->deserialize (self, keyfile, group);
}

void
sysprof_source_supplement (SysprofSource        *self,
                           SysprofCaptureReader *reader)
{
  g_return_if_fail (SYSPROF_IS_SOURCE (self));
  g_return_if_fail (reader != NULL);

  if (SYSPROF_SOURCE_GET_IFACE (self)->supplement)
    SYSPROF_SOURCE_GET_IFACE (self)->supplement (self, reader);
}
