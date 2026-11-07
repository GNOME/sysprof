/* test-cpu-kind.c
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

#include <glib/gstdio.h>
#include <sysprof.h>

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureWriter, sysprof_capture_writer_unref)

static void
assert_cpu_kinds (const char     *performance,
                  const char     *efficiency,
                  const char     *low_power_efficiency,
                  SysprofCpuKind  expected_performance,
                  SysprofCpuKind  expected_efficiency,
                  SysprofCpuKind  expected_low_power_efficiency)
{
  g_autoptr(SysprofCaptureWriter) writer = NULL;
  g_autoptr(SysprofDocumentLoader) loader = NULL;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *path = NULL;
  int fd;

  fd = g_file_open_tmp ("sysprof-cpu-kind-XXXXXX", &path, &error);
  g_assert_no_error (error);
  g_assert_cmpint (fd, >=, 0);

  writer = sysprof_capture_writer_new_from_fd (fd, 0);
  g_assert_nonnull (writer);

  if (performance != NULL)
    g_assert_true (sysprof_capture_writer_add_file (writer, SYSPROF_CAPTURE_CURRENT_TIME,
                                                    -1, -1, "/sys/devices/cpu_core/cpus", TRUE,
                                                    (const guint8 *)performance, strlen (performance)));

  if (efficiency != NULL)
    g_assert_true (sysprof_capture_writer_add_file (writer, SYSPROF_CAPTURE_CURRENT_TIME,
                                                    -1, -1, "/sys/devices/cpu_atom/cpus", TRUE,
                                                    (const guint8 *)efficiency, strlen (efficiency)));

  if (low_power_efficiency != NULL)
    g_assert_true (sysprof_capture_writer_add_file (writer, SYSPROF_CAPTURE_CURRENT_TIME,
                                                    -1, -1, "/sys/devices/cpu_lowpower/cpus", TRUE,
                                                    (const guint8 *)low_power_efficiency,
                                                    strlen (low_power_efficiency)));

  g_assert_true (sysprof_capture_writer_flush (writer));
  g_clear_pointer (&writer, sysprof_capture_writer_unref);

  loader = sysprof_document_loader_new (path);
  sysprof_document_loader_set_symbolizer (loader, sysprof_no_symbolizer_get ());
  document = sysprof_document_loader_load (loader, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (document);

  g_assert_cmpint (sysprof_document_get_cpu_kind (document, 2), ==, expected_performance);
  g_assert_cmpint (sysprof_document_get_cpu_kind (document, 5), ==, expected_efficiency);
  g_assert_cmpint (sysprof_document_get_cpu_kind (document, 8), ==, expected_low_power_efficiency);
  g_assert_cmpint (sysprof_document_get_cpu_kind (document, 12), ==, SYSPROF_CPU_KIND_UNKNOWN);
  g_assert_cmpint (sysprof_document_get_cpu_kind (document, -1), ==, SYSPROF_CPU_KIND_UNKNOWN);

  g_assert_cmpint (g_unlink (path), ==, 0);
}

static void
test_cpu_kind (void)
{
  assert_cpu_kinds ("0-3,10\n",
                    "4-7\n",
                    "8-9\n",
                    SYSPROF_CPU_KIND_PERFORMANCE,
                    SYSPROF_CPU_KIND_EFFICIENCY,
                    SYSPROF_CPU_KIND_LOW_POWER_EFFICIENCY);

  assert_cpu_kinds ("0-3,10\n",
                    "4-7\n",
                    NULL,
                    SYSPROF_CPU_KIND_PERFORMANCE,
                    SYSPROF_CPU_KIND_EFFICIENCY,
                    SYSPROF_CPU_KIND_UNKNOWN);

  assert_cpu_kinds ("0-3,10\n",
                    "4-7\n",
                    "7-8\n",
                    SYSPROF_CPU_KIND_PERFORMANCE,
                    SYSPROF_CPU_KIND_EFFICIENCY,
                    SYSPROF_CPU_KIND_UNKNOWN);

  assert_cpu_kinds ("0-3,10\n",
                    "4-7\n",
                    "8-7\n",
                    SYSPROF_CPU_KIND_PERFORMANCE,
                    SYSPROF_CPU_KIND_EFFICIENCY,
                    SYSPROF_CPU_KIND_UNKNOWN);

  assert_cpu_kinds (NULL,
                    "4-7\n",
                    "8-9\n",
                    SYSPROF_CPU_KIND_UNKNOWN,
                    SYSPROF_CPU_KIND_UNKNOWN,
                    SYSPROF_CPU_KIND_UNKNOWN);

  assert_cpu_kinds ("2-3,1\n",
                    "4-7\n",
                    NULL,
                    SYSPROF_CPU_KIND_UNKNOWN,
                    SYSPROF_CPU_KIND_UNKNOWN,
                    SYSPROF_CPU_KIND_UNKNOWN);

  assert_cpu_kinds ("0-2147483648\n",
                    "4-7\n",
                    NULL,
                    SYSPROF_CPU_KIND_UNKNOWN,
                    SYSPROF_CPU_KIND_UNKNOWN,
                    SYSPROF_CPU_KIND_UNKNOWN);

  assert_cpu_kinds ("0-5\n",
                    "4-7\n",
                    NULL,
                    SYSPROF_CPU_KIND_UNKNOWN,
                    SYSPROF_CPU_KIND_UNKNOWN,
                    SYSPROF_CPU_KIND_UNKNOWN);
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  sysprof_clock_init ();

  g_test_add_func ("/document/cpu-kind", test_cpu_kind);

  return g_test_run ();
}
