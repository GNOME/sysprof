/* sysprof-tracefd-consumer.c
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

#include <glib/gstdio.h>

#include "sysprof-instrument-private.h"
#include "sysprof-recording-private.h"
#include "sysprof-tracefd-consumer.h"

struct _SysprofTracefdConsumer
{
  SysprofInstrument parent_instance;
  int trace_fd;
};

struct _SysprofTracefdConsumerClass
{
  SysprofInstrumentClass parent_class;
};

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureReader, sysprof_capture_reader_unref)

G_DEFINE_FINAL_TYPE (SysprofTracefdConsumer, sysprof_tracefd_consumer, SYSPROF_TYPE_INSTRUMENT)

static DexFuture *
sysprof_tracefd_consumer_augment (SysprofInstrument *instrument,
                                  SysprofRecording  *recording)
{
  SysprofTracefdConsumer *self = (SysprofTracefdConsumer *)instrument;
  g_autoptr(SysprofCaptureReader) reader = NULL;
  SysprofCaptureWriter *writer;

  g_assert (SYSPROF_IS_TRACEFD_CONSUMER (self));
  g_assert (SYSPROF_IS_RECORDING (recording));

  if (self->trace_fd == -1)
    return dex_future_new_for_boolean (TRUE);

  writer = _sysprof_recording_writer (recording);

  lseek (self->trace_fd, 0, SEEK_SET);

  if ((reader = sysprof_capture_reader_new_from_fd (g_steal_fd (&self->trace_fd))))
    sysprof_capture_writer_cat (writer, reader);

  return dex_future_new_for_boolean (TRUE);
}

static void
sysprof_tracefd_consumer_finalize (GObject *object)
{
  SysprofTracefdConsumer *self = (SysprofTracefdConsumer *)object;

  g_clear_fd (&self->trace_fd, NULL);

  G_OBJECT_CLASS (sysprof_tracefd_consumer_parent_class)->finalize (object);
}

static void
sysprof_tracefd_consumer_class_init (SysprofTracefdConsumerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  object_class->finalize = sysprof_tracefd_consumer_finalize;

  instrument_class->augment = sysprof_tracefd_consumer_augment;
}

static void
sysprof_tracefd_consumer_init (SysprofTracefdConsumer *self)
{
  self->trace_fd = -1;
}

/**
 * sysprof_tracefd_consumer_new:
 * @trace_fd: a file-descriptor to read from
 *
 * This function will take ownership of @trace_fd, If you need to keep a
 * copy of your FD then it is suggested you use `dup()` to copy the file
 * descriptor.
 *
 * Returns: (transfer full): a #SysprofInstrument
 */
SysprofInstrument *
sysprof_tracefd_consumer_new (int trace_fd)
{
  SysprofTracefdConsumer *self;

  g_return_val_if_fail (trace_fd >= -1, NULL);

  self = g_object_new (SYSPROF_TYPE_TRACEFD_CONSUMER, NULL);
  self->trace_fd = trace_fd;

  return SYSPROF_INSTRUMENT (self);
}
