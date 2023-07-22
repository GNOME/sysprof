/* sysprof-document-sample.c
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

#include <glib/gi18n.h>

#include "sysprof-document-frame-private.h"

#include "sysprof-document-sample.h"
#include "sysprof-document-traceable.h"

struct _SysprofDocumentSample
{
  SysprofDocumentFrame parent_instance;
};

struct _SysprofDocumentSampleClass
{
  SysprofDocumentFrameClass parent_class;
};

enum {
  PROP_0,
  PROP_STACK_DEPTH,
  PROP_THREAD_ID,
  N_PROPS
};

static guint
sysprof_document_sample_get_stack_depth (SysprofDocumentTraceable *traceable)
{
  const SysprofCaptureSample *sample = SYSPROF_DOCUMENT_FRAME_GET (traceable, SysprofCaptureSample);

  return SYSPROF_DOCUMENT_FRAME_UINT16 (traceable, sample->n_addrs);
}

static guint64
sysprof_document_sample_get_stack_address (SysprofDocumentTraceable *traceable,
                                           guint                     position)
{
  const SysprofCaptureSample *sample = SYSPROF_DOCUMENT_FRAME_GET (traceable, SysprofCaptureSample);

  return SYSPROF_DOCUMENT_FRAME_UINT16 (traceable, sample->addrs[position]);
}

static guint
sysprof_document_sample_get_stack_addresses (SysprofDocumentTraceable *traceable,
                                             guint64                  *addresses,
                                             guint                     n_addresses)
{
  const SysprofCaptureSample *sample = SYSPROF_DOCUMENT_FRAME_GET (traceable, SysprofCaptureSample);
  guint depth = MIN (n_addresses, SYSPROF_DOCUMENT_FRAME_UINT16 (traceable, sample->n_addrs));

  for (guint i = 0; i < depth; i++)
    addresses[i] = SYSPROF_DOCUMENT_FRAME_UINT64 (traceable, sample->addrs[i]);

  return depth;
}

static int
sysprof_document_sample_get_thread_id (SysprofDocumentTraceable *traceable)
{
  SysprofDocumentSample *self = (SysprofDocumentSample *)traceable;
  const SysprofCaptureSample *sample;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_SAMPLE (self), -1);

  sample = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureSample);

  return SYSPROF_DOCUMENT_FRAME_INT32 (self, sample->tid);
}

static void
traceable_iface_init (SysprofDocumentTraceableInterface *iface)
{
  iface->get_stack_depth = sysprof_document_sample_get_stack_depth;
  iface->get_stack_address = sysprof_document_sample_get_stack_address;
  iface->get_stack_addresses = sysprof_document_sample_get_stack_addresses;
  iface->get_thread_id = sysprof_document_sample_get_thread_id;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofDocumentSample, sysprof_document_sample, SYSPROF_TYPE_DOCUMENT_FRAME,
                               G_IMPLEMENT_INTERFACE (SYSPROF_TYPE_DOCUMENT_TRACEABLE, traceable_iface_init))

static GParamSpec *properties [N_PROPS];

static void
sysprof_document_sample_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  SysprofDocumentSample *self = SYSPROF_DOCUMENT_SAMPLE (object);

  switch (prop_id)
    {
    case PROP_STACK_DEPTH:
      g_value_set_uint (value, sysprof_document_traceable_get_stack_depth (SYSPROF_DOCUMENT_TRACEABLE (self)));
      break;

    case PROP_THREAD_ID:
      g_value_set_int (value, sysprof_document_sample_get_thread_id (SYSPROF_DOCUMENT_TRACEABLE (self)));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_document_sample_class_init (SysprofDocumentSampleClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofDocumentFrameClass *document_frame_class = SYSPROF_DOCUMENT_FRAME_CLASS (klass);

  object_class->get_property = sysprof_document_sample_get_property;

  document_frame_class->type_name = N_("Sample");

  /**
   * SysprofDocumentSample:thread-id:
   *
   * The thread-id where the sample occurred.
   *
   * On Linux, this is generally set to the value of gettid().
   *
   * Since: 45
   */
  properties [PROP_THREAD_ID] =
    g_param_spec_int ("thread-id", NULL, NULL,
                      -1, G_MAXINT32, 0,
                      (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * SysprofDocumentSample:stack-depth:
   *
   * The depth of the stack trace.
   *
   * Since: 45
   */
  properties [PROP_STACK_DEPTH] =
    g_param_spec_uint ("stack-depth", NULL, NULL,
                       0, G_MAXUINT32, 0,
                       (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_document_sample_init (SysprofDocumentSample *self)
{
}
