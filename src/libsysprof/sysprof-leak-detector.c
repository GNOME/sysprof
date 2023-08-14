/* sysprof-leak-detector.c
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

#include "sysprof-document-allocation.h"
#include "sysprof-document-frame-private.h"
#include "sysprof-document-private.h"
#include "sysprof-leak-detector-private.h"

typedef struct _SysprofAllocNode SysprofAllocNode;

#include "tree.h"

struct _SysprofAllocNode
{
  RB_ENTRY(_SysprofAllocNode) link;
  guint64 address;
  gint64 size;
  guint pos;
};

static int
sysprof_alloc_node_compare (SysprofAllocNode *a,
                            SysprofAllocNode *b)
{
  if (a->address < b->address)
    return -1;
  else if (a->address > b->address)
    return 1;
  else
    return 0;
}

typedef struct _SysprofAllocs
{
  RB_HEAD(sysprof_allocs, _SysprofAllocNode) head;
} SysprofAllocs;

RB_GENERATE_STATIC(sysprof_allocs, _SysprofAllocNode, link, sysprof_alloc_node_compare);

static void
sysprof_alloc_node_free (SysprofAllocNode *node)
{
  SysprofAllocNode *right = RB_RIGHT(node, link);
  SysprofAllocNode *left = RB_LEFT(node, link);

  g_free (left);
  g_free (right);
  g_free (node);
}

EggBitset *
sysprof_leak_detector_detect (SysprofDocument *document,
                              EggBitset       *allocations)
{
  SysprofAllocs allocs;
  g_autoptr(SysprofDocumentFrame) frame = NULL;
  g_autoptr(EggBitset) leaks = NULL;
  g_autoptr(GArray) frames = NULL;
  const char *base_addr;
  EggBitsetIter iter;
  SysprofAllocNode find = {0};
  SysprofAllocNode *res;
  guint pos;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (document), NULL);
  g_return_val_if_fail (allocations != NULL, NULL);

  leaks = egg_bitset_new_empty ();

  if (!egg_bitset_iter_init_first (&iter, allocations, &pos))
    return g_steal_pointer (&leaks);

  frames = _sysprof_document_get_frames (document);

  /* Load the first allocation to look at. To avoid creating lots
   * of GObjects, we will update the internal data in the alloc
   * object to point at each subsequence record. That way it's just
   * used as a demarshalling container.
   */
  frame = g_list_model_get_item (G_LIST_MODEL (document), pos);
  frame->time_offset = 0;

  g_assert (frame != NULL);
  g_assert (SYSPROF_IS_DOCUMENT_ALLOCATION (frame));

  base_addr = g_mapped_file_get_contents (frame->mapped_file);

  RB_INIT (&allocs.head);

  for (;;)
    {
      SysprofDocumentAllocation *alloc = (SysprofDocumentAllocation *)frame;
      SysprofDocumentFramePointer *ptr = &g_array_index (frames, SysprofDocumentFramePointer, pos);
      guint64 address = sysprof_document_allocation_get_address (alloc);
      gint64 size = sysprof_document_allocation_get_size (alloc);

      /* Generally we'd want to take into account the PID as well, but
       * since we only support recording from LD_PRELOAD currently, there
       * isn't much of a chance of someone muxing multiple records into
       * a single capture. Therefore, don't waste the time on it.
       */

      if (size > 0)
        {
          SysprofAllocNode *node = g_new0 (SysprofAllocNode, 1);

          node->pos = pos;
          node->address = address;
          node->size = size;

          RB_INSERT (sysprof_allocs, &allocs.head, node);
        }
      else
        {
          find.address = address;

          if ((res = RB_FIND (sysprof_allocs, &allocs.head, &find)))
            RB_REMOVE (sysprof_allocs, &allocs.head, res);
        }

      if (!egg_bitset_iter_next (&iter, &pos))
        break;

      frame->frame = (SysprofCaptureFrame *)&base_addr[ptr->offset];
      frame->frame_len = ptr->length;
    }

  RB_FOREACH (res, sysprof_allocs, &allocs.head) {
    g_assert (res->size > 0);

    egg_bitset_add (leaks, res->pos);
  }

  if (RB_ROOT (&allocs.head))
    sysprof_alloc_node_free (RB_ROOT (&allocs.head));

  return g_steal_pointer (&leaks);
}
