/* sysprof-document-ctrdef.c
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
#include "sysprof-document-ctrdef.h"

struct _SysprofDocumentCtrdef
{
  SysprofDocumentFrame parent_instance;
};

struct _SysprofDocumentCtrdefClass
{
  SysprofDocumentFrameClass parent_class;
};

G_DEFINE_FINAL_TYPE (SysprofDocumentCtrdef, sysprof_document_ctrdef, SYSPROF_TYPE_DOCUMENT_FRAME)

static void
sysprof_document_ctrdef_class_init (SysprofDocumentCtrdefClass *klass)
{
}

static void
sysprof_document_ctrdef_init (SysprofDocumentCtrdef *self)
{
}

guint
sysprof_document_ctrdef_get_n_counters (SysprofDocumentCtrdef *self)
{
  const SysprofCaptureCounterDefine *ctrdef;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_CTRDEF (self), 0);

  ctrdef = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureCounterDefine);

  return SYSPROF_DOCUMENT_FRAME_UINT16 (self, ctrdef->n_counters);
}

/**
 * sysprof_document_ctrdef_get_counter:
 * @self: a #SysprofDocumentCtrdef
 * @nth: a value between 0 and sysprof_document_ctrdef_get_n_counters()
 * @id: (out): a location for the id of the counter
 * @type: (out): a location for either %SYSPROF_CAPTURE_COUNTER_INT64
 *   or %SYSPROF_CAPTURE_COUNTER_DOUBLE
 * @category: (out): a location for the category
 * @name: (out): a location for the name
 * @description: (out): a location for the description
 *
 * Gets information about a counter defined in @self.
 */
void
sysprof_document_ctrdef_get_counter (SysprofDocumentCtrdef  *self,
                                     guint                   nth,
                                     guint                  *id,
                                     guint                  *type,
                                     const char            **category,
                                     const char            **name,
                                     const char            **description)
{
  const SysprofCaptureCounterDefine *ctrdef;
  const SysprofCaptureCounter *ctr;

  g_return_if_fail (SYSPROF_IS_DOCUMENT_CTRDEF (self));
  g_return_if_fail (nth < sysprof_document_ctrdef_get_n_counters (self));

  ctrdef = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureCounterDefine);
  ctr = &ctrdef->counters[nth];

  if (id != NULL)
    *id = SYSPROF_DOCUMENT_FRAME_UINT32 (self, ctr->id);

  if (type != NULL)
    *type = ctr->type;

  if (category != NULL)
    *category = SYSPROF_DOCUMENT_FRAME_CSTRING (self, ctr->category);

  if (name != NULL)
    *name = SYSPROF_DOCUMENT_FRAME_CSTRING (self, ctr->name);

  if (description != NULL)
    *description = SYSPROF_DOCUMENT_FRAME_CSTRING (self, ctr->description);
}
