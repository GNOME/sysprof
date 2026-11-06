/* sysprof-document-memory-sample.c
 *
 * Copyright 2026 Christian Hergert <christian@sourceandstack.com>
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

#include <glib/gi18n.h>

#include "sysprof-document-frame-private.h"

#include "sysprof-document-memory-sample.h"
#include "sysprof-document-traceable.h"

struct _SysprofDocumentMemorySample
{
  SysprofDocumentFrame parent_instance;
};

struct _SysprofDocumentMemorySampleClass
{
  SysprofDocumentFrameClass parent_class;
};

enum
{
  PROP_0,
  PROP_STACK_DEPTH,
  PROP_THREAD_ID,
  N_PROPS
};

static guint
sysprof_document_memory_sample_get_stack_depth (SysprofDocumentTraceable *traceable)
{
  const SysprofCaptureMemorySample *sample = SYSPROF_DOCUMENT_FRAME_GET (traceable, SysprofCaptureMemorySample);

  return SYSPROF_DOCUMENT_FRAME_UINT16 (traceable, sample->n_addrs);
}

static guint64
sysprof_document_memory_sample_get_stack_address (SysprofDocumentTraceable *traceable,
                                                  guint                     position)
{
  const SysprofCaptureMemorySample *sample = SYSPROF_DOCUMENT_FRAME_GET (traceable, SysprofCaptureMemorySample);

  return SYSPROF_DOCUMENT_FRAME_UINT64 (traceable, sample->addrs[position]);
}

static guint
sysprof_document_memory_sample_get_stack_addresses (SysprofDocumentTraceable *traceable,
                                                    guint64                  *addresses,
                                                    guint                     n_addresses)
{
  const SysprofCaptureMemorySample *sample = SYSPROF_DOCUMENT_FRAME_GET (traceable, SysprofCaptureMemorySample);
  guint depth = MIN (n_addresses, SYSPROF_DOCUMENT_FRAME_UINT16 (traceable, sample->n_addrs));

  for (guint i = 0; i < depth; i++)
    addresses[i] = SYSPROF_DOCUMENT_FRAME_UINT64 (traceable, sample->addrs[i]);

  return depth;
}

static int
sysprof_document_memory_sample_get_thread_id (SysprofDocumentTraceable *traceable)
{
  SysprofDocumentMemorySample *self = (SysprofDocumentMemorySample *)traceable;
  const SysprofCaptureMemorySample *sample;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_MEMORY_SAMPLE (self), -1);

  sample = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureMemorySample);

  return SYSPROF_DOCUMENT_FRAME_INT32 (self, sample->tid);
}

static void
traceable_iface_init (SysprofDocumentTraceableInterface *iface)
{
  iface->get_stack_depth = sysprof_document_memory_sample_get_stack_depth;
  iface->get_stack_address = sysprof_document_memory_sample_get_stack_address;
  iface->get_stack_addresses = sysprof_document_memory_sample_get_stack_addresses;
  iface->get_thread_id = sysprof_document_memory_sample_get_thread_id;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofDocumentMemorySample, sysprof_document_memory_sample, SYSPROF_TYPE_DOCUMENT_FRAME,
                               G_IMPLEMENT_INTERFACE (SYSPROF_TYPE_DOCUMENT_TRACEABLE, traceable_iface_init))

static GParamSpec *properties[N_PROPS];

static void
sysprof_document_memory_sample_get_property (GObject    *object,
                                             guint       prop_id,
                                             GValue     *value,
                                             GParamSpec *pspec)
{
  SysprofDocumentMemorySample *self = SYSPROF_DOCUMENT_MEMORY_SAMPLE (object);

  switch (prop_id)
    {
    case PROP_STACK_DEPTH:
      g_value_set_uint (value, sysprof_document_traceable_get_stack_depth (SYSPROF_DOCUMENT_TRACEABLE (self)));
      break;

    case PROP_THREAD_ID:
      g_value_set_int (value, sysprof_document_memory_sample_get_thread_id (SYSPROF_DOCUMENT_TRACEABLE (self)));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_document_memory_sample_class_init (SysprofDocumentMemorySampleClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofDocumentFrameClass *document_frame_class = SYSPROF_DOCUMENT_FRAME_CLASS (klass);

  object_class->get_property = sysprof_document_memory_sample_get_property;

  document_frame_class->type_name = N_("Memory Sample");

  /**
   * SysprofDocumentMemorySample:thread-id:
   *
   * The thread-id where the sample occurred.
   *
   * On Linux, this is generally set to the value of gettid().
   *
   * Since: 52
   */
  properties[PROP_THREAD_ID] =
    g_param_spec_int ("thread-id", NULL, NULL,
                      -1, G_MAXINT32, 0,
                      (G_PARAM_READABLE |
                       G_PARAM_STATIC_STRINGS));

  /**
   * SysprofDocumentMemorySample:stack-depth:
   *
   * The depth of the stack trace.
   *
   * Since: 52
   */
  properties[PROP_STACK_DEPTH] =
    g_param_spec_uint ("stack-depth", NULL, NULL,
                       0, G_MAXUINT32, 0,
                       (G_PARAM_READABLE |
                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_document_memory_sample_init (SysprofDocumentMemorySample *self)
{
}

/**
 * sysprof_document_memory_sample_get_tid:
 * @self: a memory sample
 *
 * Returns: the thread ID
 * Since: 52
 */
int
sysprof_document_memory_sample_get_tid (SysprofDocumentMemorySample *self)
{
  const SysprofCaptureMemorySample *sample;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_MEMORY_SAMPLE (self), 0);

  sample = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureMemorySample);

  return SYSPROF_DOCUMENT_FRAME_INT32 (self, sample->tid);
}

/**
 * sysprof_document_memory_sample_get_flags:
 * @self: a memory sample
 *
 * Use these flags to check which optional fields are present.
 *
 * Returns: the validity and precision flags
 * Since: 52
 */
guint
sysprof_document_memory_sample_get_flags (SysprofDocumentMemorySample *self)
{
  const SysprofCaptureMemorySample *sample;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_MEMORY_SAMPLE (self), 0);

  sample = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureMemorySample);

  return SYSPROF_DOCUMENT_FRAME_UINT32 (self, sample->flags);
}

/**
 * sysprof_document_memory_sample_get_event_id:
 * @self: a memory sample
 *
 * Returns: the capture-local event ID
 * Since: 52
 */
guint64
sysprof_document_memory_sample_get_event_id (SysprofDocumentMemorySample *self)
{
  const SysprofCaptureMemorySample *sample;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_MEMORY_SAMPLE (self), 0);

  sample = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureMemorySample);

  return SYSPROF_DOCUMENT_FRAME_UINT64 (self, sample->event_id);
}

/**
 * sysprof_document_memory_sample_get_ip:
 * @self: a memory sample
 *
 * Returns: the sampled instruction address
 * Since: 52
 */
guint64
sysprof_document_memory_sample_get_ip (SysprofDocumentMemorySample *self)
{
  const SysprofCaptureMemorySample *sample;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_MEMORY_SAMPLE (self), 0);

  sample = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureMemorySample);

  return SYSPROF_DOCUMENT_FRAME_UINT64 (self, sample->ip);
}

/**
 * sysprof_document_memory_sample_get_address:
 * @self: a memory sample
 *
 * Returns: the accessed data address
 * Since: 52
 */
guint64
sysprof_document_memory_sample_get_address (SysprofDocumentMemorySample *self)
{
  const SysprofCaptureMemorySample *sample;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_MEMORY_SAMPLE (self), 0);

  sample = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureMemorySample);

  return SYSPROF_DOCUMENT_FRAME_UINT64 (self, sample->address);
}

/**
 * sysprof_document_memory_sample_get_period:
 * @self: a memory sample
 *
 * Returns: the sampling period
 * Since: 52
 */
guint64
sysprof_document_memory_sample_get_period (SysprofDocumentMemorySample *self)
{
  const SysprofCaptureMemorySample *sample;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_MEMORY_SAMPLE (self), 0);

  sample = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureMemorySample);

  return SYSPROF_DOCUMENT_FRAME_UINT64 (self, sample->period);
}

/**
 * sysprof_document_memory_sample_get_weight:
 * @self: a memory sample
 *
 * Returns: the raw hardware weight
 * Since: 52
 */
guint64
sysprof_document_memory_sample_get_weight (SysprofDocumentMemorySample *self)
{
  const SysprofCaptureMemorySample *sample;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_MEMORY_SAMPLE (self), 0);

  sample = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureMemorySample);

  return SYSPROF_DOCUMENT_FRAME_UINT64 (self, sample->weight);
}

/**
 * sysprof_document_memory_sample_get_data_src:
 * @self: a memory sample
 *
 * Returns: the raw Linux perf memory-source value
 * Since: 52
 */
guint64
sysprof_document_memory_sample_get_data_src (SysprofDocumentMemorySample *self)
{
  const SysprofCaptureMemorySample *sample;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_MEMORY_SAMPLE (self), 0);

  sample = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureMemorySample);

  return SYSPROF_DOCUMENT_FRAME_UINT64 (self, sample->data_src);
}

/**
 * sysprof_document_memory_sample_get_time_enabled:
 * @self: a memory sample
 *
 * Returns: the event enabled time in nanoseconds
 * Since: 52
 */
guint64
sysprof_document_memory_sample_get_time_enabled (SysprofDocumentMemorySample *self)
{
  const SysprofCaptureMemorySample *sample;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_MEMORY_SAMPLE (self), 0);

  sample = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureMemorySample);

  return SYSPROF_DOCUMENT_FRAME_UINT64 (self, sample->time_enabled);
}

/**
 * sysprof_document_memory_sample_get_time_running:
 * @self: a memory sample
 *
 * Returns: the event running time in nanoseconds
 * Since: 52
 */
guint64
sysprof_document_memory_sample_get_time_running (SysprofDocumentMemorySample *self)
{
  const SysprofCaptureMemorySample *sample;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_MEMORY_SAMPLE (self), 0);

  sample = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureMemorySample);

  return SYSPROF_DOCUMENT_FRAME_UINT64 (self, sample->time_running);
}

/**
 * sysprof_document_memory_sample_get_weight_type:
 * @self: a memory sample
 *
 * Returns: the weight encoding
 * Since: 52
 */
guint
sysprof_document_memory_sample_get_weight_type (SysprofDocumentMemorySample *self)
{
  const SysprofCaptureMemorySample *sample;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_MEMORY_SAMPLE (self), 0);

  sample = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureMemorySample);

  return SYSPROF_DOCUMENT_FRAME_UINT16 (self, sample->weight_type);
}
