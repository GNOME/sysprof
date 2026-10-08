/*
 * Copyright © 2020 Benjamin Otte
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <limits.h>
#include <glib.h>

/* Boundary powers are strictly increasing after collapsing the stack,
 * and are at most ceil (log2 (size)). Allow one entry for the newest run,
 * whose power is not known yet, and one for an append before collapsing.
 */
#define GTK_TIM_SORT_MAX_PENDING (sizeof (gsize) * CHAR_BIT + 2)

typedef struct _GtkTimSort GtkTimSort;
typedef struct _GtkTimSortRun GtkTimSortRun;

struct _GtkTimSortRun
{
  void *base;
  gsize len;
};

struct _GtkTimSort
{
  /*
   * Size of elements. Used to decide on fast paths.
   */
  gsize element_size;

  /* The comparator for this sort.
   */
  GCompareDataFunc compare_func;
  gpointer         data;

  /*
   * The unprocessed suffix of the array, and its original total length.
   */
  gpointer base;
  gsize size;
  gsize total_size;

  /*
   * The maximum size of a merge. It's guaranteed >0 and user-provided.
   * See the comments for gtk_tim_sort_set_max_merge_size() for details.
   */
  gsize max_merge_size;

  /*
   * This controls when we get *into* galloping mode.  It is initialized
   * to MIN_GALLOP.  The mergeLo and mergeHi methods nudge it higher for
   * random data, and lower for highly structured data.
   */
  gsize min_gallop;

  /*
   * The minimum run length. See compute_min_run() for details.
   */
  gsize min_run;

  /*
   * Temp storage for merges.
   */
  void *tmp;
  gsize tmp_length;

  /*
   * A stack of pending runs yet to be merged.  Run i starts at
   * address base[i] and extends for len[i] elements.  It's always
   * true (so long as the indices are in bounds) that:
   *
   *     runBase[i] + runLen[i] == runBase[i + 1]
   *
   * so we could cut the storage for this, but it's a minor amount,
   * and keeping all the info explicit simplifies the code.
   */
  gsize pending_runs;	// Number of pending runs on stack
  GtkTimSortRun run[GTK_TIM_SORT_MAX_PENDING];

  /* Powers of the boundaries to the right of the pending runs. The newest
   * run has no right neighbor yet, and its power is zero. A power is kept
   * until its boundary is removed, including across partial merges.
   */
  guint run_power[GTK_TIM_SORT_MAX_PENDING];

  /* Runs supplied by set_runs() are queued at the end of run[], in input
   * order, with only their lengths set. Introduce them into the pending
   * stack one at a time, so they establish the same power invariant as
   * newly discovered runs. The two regions of run[] never overlap.
   */
  gsize n_saved_runs;
};

void            gtk_tim_sort_init                               (GtkTimSort             *self,
                                                                 gpointer                base,
                                                                 gsize                   size,
                                                                 gsize                   element_size,
                                                                 GCompareDataFunc        compare_func,
                                                                 gpointer                data);
void            gtk_tim_sort_finish                             (GtkTimSort             *self);

void            gtk_tim_sort_get_runs                           (GtkTimSort             *self,
                                                                 gsize                   runs[GTK_TIM_SORT_MAX_PENDING + 1]);
void            gtk_tim_sort_set_runs                           (GtkTimSort             *self,
                                                                 gsize                  *runs);
void            gtk_tim_sort_set_max_merge_size                 (GtkTimSort             *self,
                                                                 gsize                   max_merge_size);

gsize           gtk_tim_sort_get_progress                       (GtkTimSort             *self);

gboolean        gtk_tim_sort_step                               (GtkTimSort             *self,
                                                                 GtkTimSortRun          *out_change);

void            gtk_tim_sort                                    (gpointer                base,
                                                                 gsize                   size,
                                                                 gsize                   element_size,
                                                                 GCompareDataFunc        compare_func,
                                                                 gpointer                user_data);

