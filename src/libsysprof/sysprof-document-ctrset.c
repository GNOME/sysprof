/* sysprof-document-ctrset.c
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

#include "sysprof-document-frame-private.h"
#include "sysprof-document-ctrset.h"

struct _SysprofDocumentCtrset
{
  SysprofDocumentFrame parent_instance;
};

struct _SysprofDocumentCtrsetClass
{
  SysprofDocumentFrameClass parent_class;
};

G_DEFINE_FINAL_TYPE (SysprofDocumentCtrset, sysprof_document_ctrset, SYSPROF_TYPE_DOCUMENT_FRAME)

static void
sysprof_document_ctrset_class_init (SysprofDocumentCtrsetClass *klass)
{
}

static void
sysprof_document_ctrset_init (SysprofDocumentCtrset *self)
{
}

guint
sysprof_document_ctrset_get_n_values (SysprofDocumentCtrset *self)
{
  const SysprofCaptureCounterSet *ctrset;
  gconstpointer endptr;
  guint n_groups;
  guint n_values = 0;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_CTRSET (self), 0);

  endptr = SYSPROF_DOCUMENT_FRAME_ENDPTR (self);
  ctrset = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureCounterSet);

  n_groups = SYSPROF_DOCUMENT_FRAME_UINT16 (self, ctrset->n_values);

  for (guint i = 0; i < n_groups; i++)
    {
      const SysprofCaptureCounterValues *values = &ctrset->values[i];

      /* Don't allow overflowing the frame zone */
      if ((gconstpointer)&values[1] > endptr)
        break;

      for (guint j = 0; j < G_N_ELEMENTS (values->ids); j++)
        {
          if (values->ids[j] == 0)
            break;
          n_values++;
        }
    }

  return n_values;
}

/**
 * sysprof_document_ctrset_get_raw_value: (skip)
 * @self: a #SysprofDocumentCtrset
 * @nth: the nth value to get
 * @id: (out): a location for the counter id
 * @value: a location to store the raw value
 *
 * The raw value is 8 bytes and may not be converted to local
 * byte ordering.
 *
 * @nth must be less-than sysprof_document_ctrset_get_n_values().
 */
void
sysprof_document_ctrset_get_raw_value (SysprofDocumentCtrset *self,
                                       guint                  nth,
                                       guint                 *id,
                                       guint8 value[restrict 8])
{
  const SysprofCaptureCounterSet *ctrset;
  guint group;
  guint pos;

  g_return_if_fail (SYSPROF_IS_DOCUMENT_CTRSET (self));
  g_return_if_fail (nth < sysprof_document_ctrset_get_n_values (self));
  g_return_if_fail (value != NULL);

  group = nth / 8;
  pos = nth % 8;

  ctrset = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureCounterSet);

  *id = SYSPROF_DOCUMENT_FRAME_UINT32 (self, ctrset->values[group].ids[pos]);

  memcpy (value, &ctrset->values[group].values[pos], 8);
}
