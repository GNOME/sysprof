/* test-vblank.c
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

#include <string.h>

#include "../sysprof-vblank-private.h"

static const char format_text[] =
  "name: drm_vblank_event\n"
  "ID: 123\n"
  "format:\n"
  "\tfield:unsigned short common_type;\toffset:0;\tsize:2;\tsigned:0;\n"
  "\tfield:int crtc;\toffset:8;\tsize:4;\tsigned:1;\n"
  "\tfield:unsigned int seq;\toffset:12;\tsize:4;\tsigned:0;\n"
  "\tfield:ktime_t time;\toffset:16;\tsize:8;\tsigned:1;\n"
  "\tfield:bool high_prec;\toffset:24;\tsize:1;\tsigned:0;\n";

static void
test_format_and_sample (void)
{
  SysprofVblankFormat format;
  guint8 raw[25] = { 0 };
  g_autofree char *bad_format = g_strdup (format_text);
  char *size;
  gint32 crtc = 2;
  guint32 sequence = 98;
  gint64 time = 123456789;
  gboolean high_prec = FALSE;

  g_assert_true (_sysprof_vblank_parse_format (format_text, &format));
  g_assert_cmpuint (format.id, ==, 123);

  memcpy (raw + 8, &crtc, 4);
  memcpy (raw + 12, &sequence, 4);
  memcpy (raw + 16, &time, 8);
  raw[24] = 1;

  g_assert_false (_sysprof_vblank_read_sample (&format, raw, 24, &crtc,
                                               &sequence, &time, &high_prec));

  g_assert_true (_sysprof_vblank_read_sample (&format, raw, sizeof raw, &crtc,
                                              &sequence, &time, &high_prec));
  g_assert_cmpuint (crtc, ==, 2);
  g_assert_cmpuint (sequence, ==, 98);
  g_assert_cmpint (time, ==, 123456789);
  g_assert_true (high_prec);

  memset (raw + 16, 0, 8);
  g_assert_false (_sysprof_vblank_read_sample (&format, raw, sizeof raw, &crtc,
                                               &sequence, &time, &high_prec));

  size = strstr (bad_format, "\tsize:4;");
  g_assert_nonnull (size);
  size[6] = '8';
  g_assert_false (_sysprof_vblank_parse_format (bad_format, &format));
}

static void
test_clocks (void)
{
  gint64 converted = 0;

  g_assert_true (_sysprof_vblank_convert_time (1000000000, 1000000000,
                                               0, 10000000000, &converted));
  g_assert_cmpint (converted, ==, 1000000000);

  g_assert_true (_sysprof_vblank_convert_time (1000000000, 11000000000,
                                               0, 10000000000, &converted));
  g_assert_cmpint (converted, ==, 11000000000);

  g_assert_true (_sysprof_vblank_convert_time (1000000000, 1000500000,
                                               0, 500000, &converted));
  g_assert_cmpint (converted, ==, 1000000000);

  g_assert_false (_sysprof_vblank_convert_time (1000000000, 1002000000,
                                                0, 2000000, &converted));

  g_assert_false (_sysprof_vblank_convert_time (1000000000, 2000000000,
                                                0, 10000000000, &converted));

  g_assert_false (_sysprof_vblank_convert_time (G_MAXINT64, G_MAXINT64,
                                                1, 10000000000, &converted));
}

static void
test_lost (void)
{
  SysprofPerfEvent event = { 0 };
  guint64 lost = 0;

  event.lost.header.type = PERF_RECORD_LOST;
  event.lost.header.size = sizeof event.lost - 1;
  event.lost.lost = 7;
  g_assert_false (_sysprof_vblank_read_lost (&event, &lost));

  event.lost.header.size = sizeof event.lost;
  g_assert_true (_sysprof_vblank_read_lost (&event, &lost));
  g_assert_cmpuint (lost, ==, 7);
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/vblank/format-and-sample", test_format_and_sample);
  g_test_add_func ("/vblank/clocks", test_clocks);
  g_test_add_func ("/vblank/lost", test_lost);

  return g_test_run ();
}
