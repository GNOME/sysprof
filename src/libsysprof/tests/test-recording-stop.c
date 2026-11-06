/* test-recording-stop.c
 *
 * Copyright 2026 Christian Hergert
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <libdex.h>

#include "sysprof-recording-private.h"

static void
stop_cb (GObject      *object,
         GAsyncResult *result,
         gpointer      user_data)
{
  GMainLoop *loop = user_data;
  g_autoptr(GError) error = NULL;

  g_assert_true (sysprof_recording_stop_finish (SYSPROF_RECORDING (object),
                                                result, &error));
  g_assert_no_error (error);
  g_main_loop_quit (loop);
}

static void
stopped_channel_cb (GObject      *object,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  GMainLoop *loop = user_data;
  g_autoptr(GError) error = NULL;

  g_assert_false (sysprof_recording_stop_finish (SYSPROF_RECORDING (object),
                                                 result, &error));
  g_assert_error (error, DEX_ERROR, DEX_ERROR_CHANNEL_CLOSED);
  g_main_loop_quit (loop);
}

static void
test_stop_finish (void)
{
  g_autoptr(SysprofRecording) recording = NULL;
  g_autoptr(DexFuture) command = NULL;
  g_autoptr(GMainLoop) loop = NULL;

  recording = g_object_new (SYSPROF_TYPE_RECORDING, NULL);
  command = dex_channel_receive (recording->channel);
  loop = g_main_loop_new (NULL, FALSE);

  sysprof_recording_stop_async (recording, NULL, stop_cb, loop);
  g_main_loop_run (loop);

  g_assert_cmpuint (dex_await_uint (g_steal_pointer (&command), NULL),
                    ==, SYSPROF_RECORDING_COMMAND_STOP);
}

static void
test_stop_closed_channel (void)
{
  g_autoptr(SysprofRecording) recording = NULL;
  g_autoptr(GMainLoop) loop = NULL;

  recording = g_object_new (SYSPROF_TYPE_RECORDING, NULL);
  loop = g_main_loop_new (NULL, FALSE);
  dex_channel_close_send (recording->channel);

  sysprof_recording_stop_async (recording, NULL, stopped_channel_cb, loop);
  g_main_loop_run (loop);
}

int
main (int   argc,
      char *argv[])
{
  dex_init ();
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/recording/stop-finish", test_stop_finish);
  g_test_add_func ("/recording/stop-closed-channel", test_stop_closed_channel);
  return g_test_run ();
}
