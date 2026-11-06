/* test-capture-counters.c
 *
 * Copyright 2026 Christian Hergert
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * Subject to the terms and conditions of this license, each copyright holder
 * and contributor hereby grants to those receiving rights under this license
 * a perpetual, worldwide, non-exclusive, no-charge, royalty-free,
 * irrevocable (except for failure to satisfy the conditions of this license)
 * patent license to make, have made, use, offer to sell, sell, import, and
 * otherwise transfer this software, where such license applies only to those
 * patent claims, already acquired or hereafter acquired, licensable by such
 * copyright holder or contributor that are necessarily infringed by:
 *
 * (a) their Contribution(s) (the licensed copyrights of copyright holders
 *     and non-copyrightable additions of contributors, in source or binary
 *     form) alone; or
 *
 * (b) combination of their Contribution(s) with the work of authorship to
 *     which such Contribution(s) was added by such copyright holder or
 *     contributor, if, at the time the Contribution is added, such addition
 *     causes such combination to be necessarily infringed. The patent license
 *     shall not apply to any other combinations which include the
 *     Contribution.
 *
 * Except as expressly stated above, no rights or licenses from any copyright
 * holder or contributor is granted under this license, whether expressly, by
 * implication, estoppel or otherwise.
 *
 * DISCLAIMER
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "config.h"

#include <glib.h>
#include <sysprof-capture.h>
#include <unistd.h>

#include "sysprof-capture-util-private.h"

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureWriter, sysprof_capture_writer_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureReader, sysprof_capture_reader_unref)

static void
test_counters (gconstpointer data)
{
  g_autoptr(SysprofCaptureWriter) writer = NULL;
  g_autoptr(SysprofCaptureReader) reader = NULL;
  g_autofree SysprofCaptureCounter *counters = NULL;
  g_autofree SysprofCaptureCounterValue *values = NULL;
  g_autofree unsigned int *ids = NULL;
  g_autofree char *filename = NULL;
  g_autoptr(GError) error = NULL;
  const guint n_counters = 6001;
  SysprofCaptureFrameType type;
  guint n_defined = 0;
  guint n_values = 0;
  guint base;
  int fd;

  fd = g_file_open_tmp ("sysprof-counters-XXXXXX", &filename, &error);
  g_assert_no_error (error);
  g_assert_cmpint (unlink (filename), ==, 0);
  writer = sysprof_capture_writer_new_from_fd (fd, GPOINTER_TO_SIZE (data));
  g_assert_nonnull (writer);
  counters = g_new0 (SysprofCaptureCounter, n_counters);
  values = g_new0 (SysprofCaptureCounterValue, n_counters);
  ids = g_new (unsigned int, n_counters);
  base = sysprof_capture_writer_request_counter (writer, n_counters);
  g_assert_cmpuint (base, >, 0);

  for (guint i = 0; i < n_counters; i++)
    {
      counters[i].id = base + i;
      counters[i].type = SYSPROF_CAPTURE_COUNTER_INT64;
      g_snprintf (counters[i].name, sizeof counters[i].name, "Counter %u", i);
      values[i].v64 = i * 100;
      ids[i] = base + i;
    }

  g_assert_true (sysprof_capture_writer_define_counters (writer, 42, 3, 123, counters, 0));
  g_assert_true (sysprof_capture_writer_set_counters (writer, 42, 3, 123, NULL, NULL, 0));
  g_assert_true (sysprof_capture_writer_define_counters (writer, 42, 3, 123, counters, n_counters));
  g_assert_true (sysprof_capture_writer_set_counters (writer, 42, 3, 123, ids, values, n_counters));
  reader = sysprof_capture_writer_create_reader (writer);
  g_assert_nonnull (reader);

  while (sysprof_capture_reader_peek_type (reader, &type))
    {
      if (type == SYSPROF_CAPTURE_FRAME_CTRDEF)
        {
          const SysprofCaptureCounterDefine *def;

          def = sysprof_capture_reader_read_counter_define (reader);
          g_assert_nonnull (def);
          g_assert_cmpuint (def->n_counters, >, 0);
          g_assert_cmpint (def->frame.time, ==, 42);
          g_assert_cmpint (def->frame.cpu, ==, 3);
          g_assert_cmpint (def->frame.pid, ==, 123);

          for (guint i = 0; i < def->n_counters; i++)
            {
              g_assert_cmpuint (n_defined, <, n_counters);
              g_assert_cmpmem (&def->counters[i], sizeof counters[0],
                               &counters[n_defined], sizeof counters[0]);
              n_defined++;
            }
        }
      else if (type == SYSPROF_CAPTURE_FRAME_CTRSET)
        {
          const SysprofCaptureCounterSet *set;

          set = sysprof_capture_reader_read_counter_set (reader);
          g_assert_nonnull (set);
          g_assert_cmpuint (set->n_values, >, 0);
          g_assert_cmpint (set->frame.time, ==, 42);
          g_assert_cmpint (set->frame.cpu, ==, 3);
          g_assert_cmpint (set->frame.pid, ==, 123);

          for (guint i = 0; i < set->n_values; i++)
            for (guint j = 0; j < G_N_ELEMENTS (set->values[i].ids); j++)
              {
                if (set->values[i].ids[j] == 0)
                  continue;

                g_assert_cmpuint (n_values, <, n_counters);
                g_assert_cmpuint (set->values[i].ids[j], ==, ids[n_values]);
                g_assert_cmpint (set->values[i].values[j].v64, ==, values[n_values].v64);
                n_values++;
              }
        }
      else
        g_assert_true (sysprof_capture_reader_skip (reader));
    }

  g_assert_cmpuint (n_defined, ==, n_counters);
  g_assert_cmpuint (n_values, ==, n_counters);
}

int
main (int   argc,
      char *argv[])
{
  sysprof_clock_init ();
  g_test_init (&argc, &argv, NULL);
  g_test_add_data_func ("/capture/counters/default-buffer", NULL, test_counters);
  g_test_add_data_func ("/capture/counters/small-buffer", GSIZE_TO_POINTER (_sysprof_getpagesize ()), test_counters);
  return g_test_run ();
}
