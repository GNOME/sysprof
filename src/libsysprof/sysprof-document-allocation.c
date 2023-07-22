/* sysprof-document-allocation.c
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

#include "sysprof-document-allocation.h"
#include "sysprof-document-traceable.h"

struct _SysprofDocumentAllocation
{
  SysprofDocumentFrame parent_instance;
};

struct _SysprofDocumentAllocationClass
{
  SysprofDocumentFrameClass parent_class;
};

enum {
  PROP_0,
  PROP_ADDRESS,
  PROP_IS_FREE,
  PROP_SIZE,
  PROP_STACK_DEPTH,
  PROP_THREAD_ID,
  N_PROPS
};

static guint
sysprof_document_allocation_get_stack_depth (SysprofDocumentTraceable *traceable)
{
  const SysprofCaptureAllocation *allocation = SYSPROF_DOCUMENT_FRAME_GET (traceable, SysprofCaptureAllocation);

  return SYSPROF_DOCUMENT_FRAME_UINT16 (traceable, allocation->n_addrs);
}

static guint64
sysprof_document_allocation_get_stack_address (SysprofDocumentTraceable *traceable,
                                               guint                     position)
{
  const SysprofCaptureAllocation *allocation = SYSPROF_DOCUMENT_FRAME_GET (traceable, SysprofCaptureAllocation);

  return SYSPROF_DOCUMENT_FRAME_UINT16 (traceable, allocation->addrs[position]);
}

static guint
sysprof_document_allocation_get_stack_addresses (SysprofDocumentTraceable *traceable,
                                                 guint64                  *addresses,
                                                 guint                     n_addresses)
{
  const SysprofCaptureAllocation *allocation = SYSPROF_DOCUMENT_FRAME_GET (traceable, SysprofCaptureAllocation);
  guint depth = MIN (n_addresses, SYSPROF_DOCUMENT_FRAME_UINT16 (traceable, allocation->n_addrs));

  for (guint i = 0; i < depth; i++)
    addresses[i] = SYSPROF_DOCUMENT_FRAME_UINT64 (traceable, allocation->addrs[i]);

  return depth;
}

static int
sysprof_document_allocation_get_thread_id (SysprofDocumentTraceable *traceable)
{
  SysprofDocumentAllocation *self = (SysprofDocumentAllocation *)traceable;
  const SysprofCaptureAllocation *allocation;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_ALLOCATION (self), 0);

  allocation = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureAllocation);

  return SYSPROF_DOCUMENT_FRAME_INT32 (self, allocation->tid);
}

static void
traceable_iface_init (SysprofDocumentTraceableInterface *iface)
{
  iface->get_stack_depth = sysprof_document_allocation_get_stack_depth;
  iface->get_stack_address = sysprof_document_allocation_get_stack_address;
  iface->get_stack_addresses = sysprof_document_allocation_get_stack_addresses;
  iface->get_thread_id = sysprof_document_allocation_get_thread_id;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofDocumentAllocation, sysprof_document_allocation, SYSPROF_TYPE_DOCUMENT_FRAME,
                               G_IMPLEMENT_INTERFACE (SYSPROF_TYPE_DOCUMENT_TRACEABLE, traceable_iface_init))

static GParamSpec *properties [N_PROPS];

static void
sysprof_document_allocation_get_property (GObject    *object,
                                          guint       prop_id,
                                          GValue     *value,
                                          GParamSpec *pspec)
{
  SysprofDocumentAllocation *self = SYSPROF_DOCUMENT_ALLOCATION (object);

  switch (prop_id)
    {
    case PROP_ADDRESS:
      g_value_set_uint64 (value, sysprof_document_allocation_get_address (self));
      break;

    case PROP_IS_FREE:
      g_value_set_boolean (value, sysprof_document_allocation_is_free (self));
      break;

    case PROP_SIZE:
      g_value_set_int64 (value, sysprof_document_allocation_get_size (self));
      break;

    case PROP_STACK_DEPTH:
      g_value_set_uint (value, sysprof_document_traceable_get_stack_depth (SYSPROF_DOCUMENT_TRACEABLE (self)));
      break;

    case PROP_THREAD_ID:
      g_value_set_int (value, sysprof_document_traceable_get_thread_id (SYSPROF_DOCUMENT_TRACEABLE (self)));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_document_allocation_class_init (SysprofDocumentAllocationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SysprofDocumentFrameClass *document_frame_class = SYSPROF_DOCUMENT_FRAME_CLASS (klass);

  object_class->get_property = sysprof_document_allocation_get_property;

  document_frame_class->type_name = N_("Allocation");

  /**
   * SysprofDocumentAllocation:thread-id:
   *
   * The thread-id where the stack was traced.
   *
   * On Linux, this is generally set to the value of `gettid()`.
   *
   * Since: 45
   */
  properties [PROP_THREAD_ID] =
    g_param_spec_int ("thread-id", NULL, NULL,
                      -1, G_MAXINT32, 0,
                      (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * SysprofDocumentAllocation:address:
   *
   * The address that was allocated or freed.
   *
   * Since: 45
   */
  properties [PROP_ADDRESS] =
    g_param_spec_uint64 ("address", NULL, NULL,
                         0, G_MAXUINT64, 0,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * SysprofDocumentAllocation:size:
   *
   * The size of the memory that was allocated or freed.
   *
   * Since: 45
   */
  properties [PROP_SIZE] =
    g_param_spec_int64 ("size", NULL, NULL,
                        G_MININT64, G_MAXINT64, 0,
                        (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * SysprofDocumentAllocation:is-free:
   *
   * If this allocation record is a call to release
   * #SysprofDocumentAllocation:address.
   *
   * Since: 45
   */
  properties [PROP_IS_FREE] =
    g_param_spec_boolean ("is-free", NULL, NULL,
                          FALSE,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * SysprofDocumentAllocation:stack-depth:
   *
   * The depth of the stack trace.
   *
   * Since: 45
   */
  properties [PROP_STACK_DEPTH] =
    g_param_spec_uint ("stack-depth", NULL, NULL,
                       0, G_MAXUINT16, 0,
                       (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_document_allocation_init (SysprofDocumentAllocation *self)
{
}

guint64
sysprof_document_allocation_get_address (SysprofDocumentAllocation *self)
{
  const SysprofCaptureAllocation *allocation;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_ALLOCATION (self), 0);

  allocation = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureAllocation);

  return SYSPROF_DOCUMENT_FRAME_UINT64 (self, allocation->alloc_addr);
}

gint64
sysprof_document_allocation_get_size (SysprofDocumentAllocation *self)
{
  const SysprofCaptureAllocation *allocation;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_ALLOCATION (self), 0);

  allocation = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureAllocation);

  return SYSPROF_DOCUMENT_FRAME_INT64 (self, allocation->alloc_size);
}

gboolean
sysprof_document_allocation_is_free (SysprofDocumentAllocation *self)
{
  const SysprofCaptureAllocation *allocation;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_ALLOCATION (self), 0);

  allocation = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureAllocation);

  return allocation->alloc_size == 0;
}
