/* sp-source.c
 *
 * Copyright (C) 2016 Christian Hergert <chergert@redhat.com>
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
 */

#include "sources/sp-source.h"

G_DEFINE_INTERFACE (SpSource, sp_source, G_TYPE_OBJECT)

enum {
  FAILED,
  FINISHED,
  READY,
  N_SIGNALS
};

static guint signals [N_SIGNALS];

static void
sp_source_default_init (SpSourceInterface *iface)
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
sp_source_add_pid (SpSource *self,
                   GPid      pid)
{
  g_return_if_fail (SP_IS_SOURCE (self));
  g_return_if_fail (pid != FALSE);

  if (SP_SOURCE_GET_IFACE (self)->add_pid)
    SP_SOURCE_GET_IFACE (self)->add_pid (self, pid);
}

void
sp_source_emit_finished (SpSource *self)
{
  g_return_if_fail (SP_IS_SOURCE (self));

  g_signal_emit (self, signals [FINISHED], 0);
}

void
sp_source_emit_failed (SpSource     *self,
                       const GError *error)
{
  g_return_if_fail (SP_IS_SOURCE (self));
  g_return_if_fail (error != NULL);

  g_signal_emit (self, signals [FAILED], 0, error);
}

void
sp_source_emit_ready (SpSource *self)
{
  g_return_if_fail (SP_IS_SOURCE (self));

  g_signal_emit (self, signals [READY], 0);
}

gboolean
sp_source_get_is_ready (SpSource *self)
{
  g_return_val_if_fail (SP_IS_SOURCE (self), FALSE);

  if (SP_SOURCE_GET_IFACE (self)->get_is_ready)
    return SP_SOURCE_GET_IFACE (self)->get_is_ready (self);

  return TRUE;
}

void
sp_source_prepare (SpSource *self)
{
  g_return_if_fail (SP_IS_SOURCE (self));

  if (SP_SOURCE_GET_IFACE (self)->prepare)
    SP_SOURCE_GET_IFACE (self)->prepare (self);
}

void
sp_source_set_writer (SpSource        *self,
                      SpCaptureWriter *writer)
{
  g_return_if_fail (SP_IS_SOURCE (self));
  g_return_if_fail (writer != NULL);

  if (SP_SOURCE_GET_IFACE (self)->set_writer)
    SP_SOURCE_GET_IFACE (self)->set_writer (self, writer);
}

void
sp_source_start (SpSource *self)
{
  g_return_if_fail (SP_IS_SOURCE (self));

  if (SP_SOURCE_GET_IFACE (self)->start)
    SP_SOURCE_GET_IFACE (self)->start (self);
}

void
sp_source_stop (SpSource *self)
{
  g_return_if_fail (SP_IS_SOURCE (self));

  if (SP_SOURCE_GET_IFACE (self)->stop)
    SP_SOURCE_GET_IFACE (self)->stop (self);
}
