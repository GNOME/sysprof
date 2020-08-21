/* sysprof-turbostat.c
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
 * SPDX-License-ntifier: GPL-3.0-or-later
 */

#include "sysprof-turbostat.h"

#include <errno.h>
#include <glib-unix.h>
#include <math.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct _SysprofTurbostat
{
  volatile gint ref_count;
  GPid          pid;
  GIOChannel   *channel;
  guint         channel_watch;
  GFunc         sample_func;
  gpointer      sample_data;
};

enum {
  KIND_0,
  KIND_DOUBLE,
  KIND_INT,
};

SysprofTurbostat *
sysprof_turbostat_new (GFunc    sample_func,
                       gpointer sample_data)
{
  SysprofTurbostat *self;

  self = g_slice_new0 (SysprofTurbostat);
  self->ref_count = 1;
  self->pid = 0;
  self->channel = NULL;
  self->sample_func = sample_func;
  self->sample_data = sample_data;

  return g_steal_pointer (&self);
}

static void
sysprof_turbostat_finalize (SysprofTurbostat *self)
{
  if (self->pid != 0)
    sysprof_turbostat_stop (self);

  g_assert (self->pid == 0);
  g_assert (self->channel == NULL);

  g_slice_free (SysprofTurbostat, self);
}

SysprofTurbostat *
sysprof_turbostat_ref (SysprofTurbostat *self)
{
  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (self->ref_count > 0, NULL);

  g_atomic_int_inc (&self->ref_count);

  return self;
}

void
sysprof_turbostat_unref (SysprofTurbostat *self)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (self->ref_count > 0);

  if (g_atomic_int_dec_and_test (&self->ref_count))
    sysprof_turbostat_finalize (self);
}

static gboolean
sysprof_turbostat_watch_cb (GIOChannel   *channel,
                            GIOCondition  cond,
                            gpointer      data)
{
  SysprofTurbostat *self = data;
  g_autoptr(GArray) ret = NULL;
  g_autoptr(GString) str = NULL;
  g_auto(GStrv) columns = NULL;
  GIOStatus r;
  gint lineno = 0;

  g_assert (channel != NULL);
  g_assert (cond & G_IO_IN);

  ret = g_array_new (FALSE, FALSE, sizeof (SysprofTurbostatSample));
  str = g_string_new (NULL);

  for (;;)
    {
      SysprofTurbostatSample sample = {0};
      g_autoptr(GError) lerror = NULL;
      g_auto(GStrv) parts = NULL;
      gsize pos = 0;

      lineno++;

      r = g_io_channel_read_line_string (self->channel, str, &pos, &lerror);
      if (r != G_IO_STATUS_NORMAL || str->len == 0 || pos == 0)
        break;

      g_string_truncate (str, pos - 1);

      parts = g_strsplit (str->str, "\t", 0);

      if (lineno == 1)
        {
          columns = g_steal_pointer (&parts);
          continue;
        }

      g_assert (columns != NULL);

      for (guint i = 0; columns[i] != NULL && parts[i] != NULL; i++)
        {
          gdouble *addr = NULL;
          gint *iaddr = NULL;
          int kind = KIND_0;

          if (g_strcmp0 (columns[i], "GFXWatt") == 0)
            {
              addr = &sample.gfx_watt;
              kind = KIND_DOUBLE;
            }
          else if (g_strcmp0 (columns[i], "CorWatt") == 0)
            {
              addr = &sample.core_watt;
              kind = KIND_DOUBLE;
            }
          else if (g_strcmp0 (columns[i], "PkgWatt") == 0)
            {
              addr = &sample.pkg_watt;
              kind = KIND_DOUBLE;
            }
          else if (g_strcmp0 (columns[i], "RAMWatt") == 0)
            {
              addr = &sample.ram_watt;
              kind = KIND_DOUBLE;
            }
          else if (g_strcmp0 (columns[i], "Core") == 0)
            {
              iaddr = &sample.core;
              kind = KIND_INT;
            }
          else if (g_strcmp0 (columns[i], "CPU") == 0)
            {
              iaddr = &sample.cpu;
              kind = KIND_INT;
            }

          if (kind == KIND_0)
            continue;

          g_assert ((kind == KIND_DOUBLE && addr != NULL) ||
                    (kind == KIND_INT && iaddr != NULL));

          if (kind == KIND_DOUBLE)
            {
              *addr = g_ascii_strtod (parts[i], NULL);

              /* Reset to zero if we failed to parse */
              if (*addr == HUGE_VAL && errno == ERANGE)
                *addr = 0.0;
            }
          else if (kind == KIND_INT)
            {
              /* Some columns are "-" and we use -1 to indicate that. */
              if (parts[i][0] == '-' && parts[i][1] == 0)
                {
                  *iaddr = -1;
                }
              else
                {
                  gint64 v = g_ascii_strtoll (parts[i], NULL, 10);

                  /* Reset if we failed to parse */
                  if ((v == G_MAXINT64 || v == G_MININT64) && errno == ERANGE)
                    v = 0;

                  *iaddr = v;
                }
            }
        }

      g_array_append_val (ret, sample);
    }

  if (ret->len > 0)
    self->sample_func (ret, self->sample_data);

  return G_SOURCE_CONTINUE;
}

gboolean
sysprof_turbostat_start (SysprofTurbostat  *self,
                         GError           **error)
{
  /* We use a long interval and kill(..., SIGUSR1) to force a sample */
  static const gchar *argv[] = { "turbostat", "-T", "Celcius", "-i", "100000", NULL };
  g_auto(GStrv) env = NULL;
  gboolean ret;
  gint stdout_fd = -1;

  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (self->pid == 0, FALSE);
  g_return_val_if_fail (self->channel == NULL, FALSE);

  env = g_get_environ ();
  env = g_environ_setenv (env, "LANG", "C", TRUE);

  ret = g_spawn_async_with_pipes (NULL,
                                (gchar **)argv,
                                env,
                                (G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL),
                                NULL,
                                NULL,
                                &self->pid,
                                NULL,
                                &stdout_fd,
                                NULL,
                                error);


  if (ret)
    {
      if (!g_unix_set_fd_nonblocking (stdout_fd, TRUE, error))
        {
          ret = FALSE;
          close (stdout_fd);
        }

      self->channel = g_io_channel_unix_new (stdout_fd);
      g_io_channel_set_close_on_unref (self->channel, TRUE);
      g_io_channel_set_buffer_size (self->channel, 4096);
      g_io_channel_set_flags (self->channel, G_IO_FLAG_NONBLOCK, NULL);
      self->channel_watch =
        g_io_add_watch (self->channel,
                        G_IO_IN,
                        sysprof_turbostat_watch_cb,
                        self);
    }

  return ret;
}

void
sysprof_turbostat_stop (SysprofTurbostat *self)
{
  g_return_if_fail (self != NULL);

  if (self->pid != 0)
    {
      GPid pid = self->pid;
      self->pid = 0;
      kill (pid, SIGTERM);
    }

  g_clear_handle_id (&self->channel_watch, g_source_remove);
  g_clear_pointer (&self->channel, g_io_channel_unref);
}

gboolean
sysprof_turbostat_sample (SysprofTurbostat  *self,
                          GError           **error)
{
  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (self->channel != NULL, FALSE);
  g_return_val_if_fail (self->pid != 0, FALSE);

  kill (self->pid, SIGUSR1);

  return TRUE;
}
