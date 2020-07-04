/* sysprof-memprof-profile.c
 *
 * Copyright 2020 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sysprof-memprof-profile"

#include "config.h"

#include <sysprof-capture.h>

#include "sysprof-capture-autocleanups.h"
#include "sysprof-capture-symbol-resolver.h"
#include "sysprof-elf-symbol-resolver.h"
#include "sysprof-kernel-symbol-resolver.h"
#include "sysprof-memprof-profile.h"
#include "sysprof-symbol-resolver.h"

#include "rax.h"
#include "../stackstash.h"

typedef struct
{
  gint                  pid;
  gint                  tid;
  gint64                time;
  SysprofCaptureAddress addr;
  gint64                size;
  guint64               frame_num;
} Alloc;

typedef struct
{
  volatile gint         ref_count;
  SysprofSelection     *selection;
  SysprofCaptureReader *reader;
  GPtrArray            *resolvers;
  GStringChunk         *symbols;
  GHashTable           *tags;
  GHashTable           *cmdlines;
  StackStash           *stash;
  StackStash           *building;
  rax                  *rax;
  GArray               *resolved;
  SysprofMemprofMode    mode;
  SysprofMemprofStats   stats;
} Generate;

struct _SysprofMemprofProfile
{
  GObject               parent_instance;
  SysprofSelection     *selection;
  SysprofCaptureReader *reader;
  Generate             *g;
  SysprofMemprofMode    mode;
};

static void profile_iface_init (SysprofProfileInterface *iface);

G_DEFINE_TYPE_WITH_CODE (SysprofMemprofProfile, sysprof_memprof_profile, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (SYSPROF_TYPE_PROFILE, profile_iface_init))

enum {
  PROP_0,
  PROP_SELECTION,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
generate_finalize (Generate *g)
{
  g_clear_pointer (&g->reader, sysprof_capture_reader_unref);
  g_clear_pointer (&g->rax, raxFree);
  g_clear_pointer (&g->stash, stack_stash_unref);
  g_clear_pointer (&g->building, stack_stash_unref);
  g_clear_pointer (&g->resolvers, g_ptr_array_unref);
  g_clear_pointer (&g->symbols, g_string_chunk_free);
  g_clear_pointer (&g->tags, g_hash_table_unref);
  g_clear_pointer (&g->resolved, g_array_unref);
  g_clear_pointer (&g->cmdlines, g_hash_table_unref);
  g_clear_object (&g->selection);
  g_slice_free (Generate, g);
}

static Generate *
generate_ref (Generate *g)
{
  g_return_val_if_fail (g != NULL, NULL);
  g_return_val_if_fail (g->ref_count > 0, NULL);

  g_atomic_int_inc (&g->ref_count);

  return g;
}

static void
generate_unref (Generate *g)
{
  g_return_if_fail (g != NULL);
  g_return_if_fail (g->ref_count > 0);

  if (g_atomic_int_dec_and_test (&g->ref_count))
    generate_finalize (g);
}

static void
sysprof_memprof_profile_finalize (GObject *object)
{
  SysprofMemprofProfile *self = (SysprofMemprofProfile *)object;

  g_clear_pointer (&self->g, generate_unref);
  g_clear_pointer (&self->reader, sysprof_capture_reader_unref);
  g_clear_object (&self->selection);

  G_OBJECT_CLASS (sysprof_memprof_profile_parent_class)->finalize (object);
}

static void
sysprof_memprof_profile_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  SysprofMemprofProfile *self = SYSPROF_MEMPROF_PROFILE (object);

  switch (prop_id)
    {
    case PROP_SELECTION:
      g_value_set_object (value, self->selection);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_memprof_profile_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  SysprofMemprofProfile *self = SYSPROF_MEMPROF_PROFILE (object);

  switch (prop_id)
    {
    case PROP_SELECTION:
      self->selection = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_memprof_profile_class_init (SysprofMemprofProfileClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_memprof_profile_finalize;
  object_class->get_property = sysprof_memprof_profile_get_property;
  object_class->set_property = sysprof_memprof_profile_set_property;

  properties [PROP_SELECTION] =
    g_param_spec_object ("selection",
                         "Selection",
                         "The selection for filtering the callgraph",
                         SYSPROF_TYPE_SELECTION,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_memprof_profile_init (SysprofMemprofProfile *self)
{
  self->mode = SYSPROF_MEMPROF_MODE_ALL_ALLOCS;
}

SysprofMemprofMode
sysprof_memprof_profile_get_mode (SysprofMemprofProfile *self)
{
  g_return_val_if_fail (SYSPROF_IS_MEMPROF_PROFILE (self), 0);

  return self->mode;
}

void
sysprof_memprof_profile_set_mode (SysprofMemprofProfile *self,
                                  SysprofMemprofMode     mode)
{
  g_return_if_fail (SYSPROF_IS_MEMPROF_PROFILE (self));

  self->mode = mode;
}

SysprofProfile *
sysprof_memprof_profile_new (void)
{
  return g_object_new (SYSPROF_TYPE_MEMPROF_PROFILE, NULL);
}

static void
sysprof_memprof_profile_set_reader (SysprofProfile       *profile,
                                    SysprofCaptureReader *reader)
{
  SysprofMemprofProfile *self = (SysprofMemprofProfile *)profile;

  g_assert (SYSPROF_IS_MEMPROF_PROFILE (self));
  g_assert (reader != NULL);

  if (reader != self->reader)
    {
      g_clear_pointer (&self->reader, sysprof_capture_reader_unref);
      self->reader = sysprof_capture_reader_ref (reader);
    }
}

static SysprofCaptureCursor *
create_cursor (SysprofCaptureReader *reader)
{
  static SysprofCaptureFrameType types[] = {
    SYSPROF_CAPTURE_FRAME_ALLOCATION,
    SYSPROF_CAPTURE_FRAME_PROCESS,
  };
  SysprofCaptureCursor *cursor;
  SysprofCaptureCondition *cond;

  cond = sysprof_capture_condition_new_where_type_in (G_N_ELEMENTS (types), types);
  cursor = sysprof_capture_cursor_new (reader);
  sysprof_capture_cursor_add_condition (cursor, cond);

  return cursor;
}

static bool
all_allocs_foreach_cb (const SysprofCaptureFrame *frame,
                       void                      *user_data)
{
  Generate *g = user_data;

  g_assert (frame != NULL);
  g_assert (frame->type == SYSPROF_CAPTURE_FRAME_ALLOCATION ||
            frame->type == SYSPROF_CAPTURE_FRAME_PROCESS);

  if G_UNLIKELY (frame->type == SYSPROF_CAPTURE_FRAME_PROCESS)
    {
      const SysprofCaptureProcess *pr = (const SysprofCaptureProcess *)frame;

      if (!g_hash_table_contains (g->cmdlines, GINT_TO_POINTER (frame->pid)))
        {
          g_autofree gchar *cmdline = g_strdup_printf ("[%s]", pr->cmdline);
          g_hash_table_insert (g->cmdlines,
                               GINT_TO_POINTER (frame->pid),
                               (gchar *)g_string_chunk_insert_const (g->symbols, cmdline));
        }

      return true;
    }

  /* Short-circuit if we don't care about this frame */
  if (!sysprof_selection_contains (g->selection, frame->time))
    return true;

  if (frame->type == SYSPROF_CAPTURE_FRAME_ALLOCATION)
    {
      const SysprofCaptureAllocation *ev = (const SysprofCaptureAllocation *)frame;

      /* Handle memory allocations */
      if (ev->alloc_size > 0)
        {
          SysprofAddressContext last_context = SYSPROF_ADDRESS_CONTEXT_NONE;
          const gchar *cmdline;
          StackNode *node;
          guint len = 5;

          node = stack_stash_add_trace (g->building, ev->addrs, ev->n_addrs, ev->alloc_size);

          for (const StackNode *iter = node; iter != NULL; iter = iter->parent)
            len++;

          if (G_UNLIKELY (g->resolved->len < len))
            g_array_set_size (g->resolved, len);

          len = 0;

          for (const StackNode *iter = node; iter != NULL; iter = iter->parent)
            {
              SysprofAddressContext context = SYSPROF_ADDRESS_CONTEXT_NONE;
              SysprofAddress address = iter->data;
              const gchar *symbol = NULL;

              if (sysprof_address_is_context_switch (address, &context))
                {
                  if (last_context)
                    symbol = sysprof_address_context_to_string (last_context);
                  else
                    symbol = NULL;

                  last_context = context;
                }
              else
                {
                  for (guint i = 0; i < g->resolvers->len; i++)
                    {
                      SysprofSymbolResolver *resolver = g_ptr_array_index (g->resolvers, i);
                      GQuark tag = 0;
                      gchar *str;

                      str = sysprof_symbol_resolver_resolve_with_context (resolver,
                                                                          frame->time,
                                                                          frame->pid,
                                                                          last_context,
                                                                          address,
                                                                          &tag);

                      if (str != NULL)
                        {
                          symbol = g_string_chunk_insert_const (g->symbols, str);
                          g_free (str);

                          if (tag != 0 && !g_hash_table_contains (g->tags, symbol))
                            g_hash_table_insert (g->tags, (gchar *)symbol, GSIZE_TO_POINTER (tag));

                          break;
                        }
                    }
                }

              if (symbol != NULL)
                g_array_index (g->resolved, SysprofAddress, len++) = POINTER_TO_U64 (symbol);
            }

          if ((cmdline = g_hash_table_lookup (g->cmdlines, GINT_TO_POINTER (frame->pid))))
            g_array_index (g->resolved, guint64, len++) = POINTER_TO_U64 (cmdline);

          g_array_index (g->resolved, guint64, len++) = POINTER_TO_U64 ("[Everything]");

          stack_stash_add_trace (g->stash,
                                 (gpointer)g->resolved->data,
                                 len,
                                 ev->alloc_size);
        }
    }

  return true;
}

static gint
compare_frame_num_reverse (gconstpointer a,
                           gconstpointer b)
{
  const Alloc *aptr = a;
  const Alloc *bptr = b;

  if (aptr->frame_num < bptr->frame_num)
    return 1;
  else if (aptr->frame_num > bptr->frame_num)
    return -1;
  else
    return 0;
}


static gint
compare_alloc (gconstpointer a,
               gconstpointer b)
{
  const Alloc *aptr = a;
  const Alloc *bptr = b;

  if (aptr->pid < bptr->pid)
    return -1;
  else if (aptr->pid > bptr->pid)
    return 1;

  if (aptr->tid < bptr->tid)
    return -1;
  else if (aptr->tid > bptr->tid)
    return 1;

  if (aptr->time < bptr->time)
    return -1;
  else if (aptr->time > bptr->time)
    return 1;

  if (aptr->addr < bptr->addr)
    return -1;
  else if (aptr->addr > bptr->addr)
    return 1;
  else
    return 0;
}

static guint
get_bucket (gint64 size)
{
  if (size <= 32)
    return 0;
  if (size <= 64)
    return 1;
  if (size <= 128)
    return 2;
  if (size <= 256)
    return 3;
  if (size <= 512)
    return 4;
  if (size <= 1024)
    return 5;
  if (size <= 4096)
    return 6;
  if (size <= 4096*4)
    return 7;
  if (size <= 4096*8)
    return 8;
  if (size <= 4096*16)
    return 9;
  if (size <= 4096*32)
    return 10;
  if (size <= 4096*64)
    return 11;
  if (size <= 4096*256)
    return 12;
  return 13;
}

static void
summary_worker (Generate *g)
{
  g_autoptr(GArray) allocs = NULL;
  SysprofCaptureFrameType type;
  SysprofCaptureAddress last_addr = 0;
  guint last_bucket = 0;

  g_assert (g != NULL);
  g_assert (g->reader != NULL);

  allocs = g_array_new (FALSE, FALSE, sizeof (Alloc));

  sysprof_capture_reader_reset (g->reader);

  g->stats.by_size[0].bucket = 32;
  g->stats.by_size[1].bucket = 64;
  g->stats.by_size[2].bucket = 128;
  g->stats.by_size[3].bucket = 256;
  g->stats.by_size[4].bucket = 512;
  g->stats.by_size[5].bucket = 1024;
  g->stats.by_size[6].bucket = 4096;
  g->stats.by_size[7].bucket = 4096*4;
  g->stats.by_size[8].bucket = 4096*8;
  g->stats.by_size[9].bucket = 4096*16;
  g->stats.by_size[10].bucket = 4096*32;
  g->stats.by_size[11].bucket = 4096*64;
  g->stats.by_size[12].bucket = 4096*256;
  g->stats.by_size[13].bucket = 4096*256;

  while (sysprof_capture_reader_peek_type (g->reader, &type))
    {
      if (type == SYSPROF_CAPTURE_FRAME_ALLOCATION)
        {
          const SysprofCaptureAllocation *ev;
          Alloc a;

          if (!(ev = sysprof_capture_reader_read_allocation (g->reader)))
            break;

          a.pid = ev->frame.pid;
          a.tid = ev->tid;
          a.time = ev->frame.time;
          a.addr = ev->alloc_addr;
          a.size = ev->alloc_size;
          a.frame_num = 0;

          g_array_append_val (allocs, a);

          if (a.size > 0)
            g->stats.n_allocs++;
        }
      else
        {
          if (!sysprof_capture_reader_skip (g->reader))
            break;
        }
    }

  g_array_sort (allocs, compare_alloc);

  for (guint i = 0; i < allocs->len; i++)
    {
      const Alloc *a = &g_array_index (allocs, Alloc, i);

      if (a->size <= 0)
        {
          if (last_addr == a->addr)
            {
              g->stats.temp_allocs++;
              g->stats.by_size[last_bucket].temp_allocs++;
            }

          g->stats.leaked_allocs--;

          last_addr = 0;
          last_bucket = 0;
        }
      else
        {
          guint b = get_bucket (a->size);

          g->stats.n_allocs++;
          g->stats.leaked_allocs++;
          g->stats.by_size[b].n_allocs++;
          g->stats.by_size[b].allocated += a->size;

          last_addr = a->addr;
          last_bucket = b;
        }
    }
}

static void
temp_allocs_worker (Generate *g)
{
  g_autoptr(GArray) temp_allocs = NULL;
  g_autoptr(GArray) all_allocs = NULL;
  StackNode *node;
  SysprofCaptureFrameType type;
  SysprofCaptureAddress last_addr = 0;
  guint64 frame_num = 0;

  g_assert (g != NULL);
  g_assert (g->reader != NULL);

  temp_allocs = g_array_new (FALSE, FALSE, sizeof (Alloc));
  all_allocs = g_array_new (FALSE, FALSE, sizeof (Alloc));

  sysprof_capture_reader_reset (g->reader);

  while (sysprof_capture_reader_peek_type (g->reader, &type))
    {
      if G_UNLIKELY (type == SYSPROF_CAPTURE_FRAME_PROCESS)
        {
          const SysprofCaptureProcess *pr;

          if (!(pr = sysprof_capture_reader_read_process (g->reader)))
            break;

          if (!g_hash_table_contains (g->cmdlines, GINT_TO_POINTER (pr->frame.pid)))
            {
              g_autofree gchar *cmdline = g_strdup_printf ("[%s]", pr->cmdline);
              g_hash_table_insert (g->cmdlines,
                                   GINT_TO_POINTER (pr->frame.pid),
                                   (gchar *)g_string_chunk_insert_const (g->symbols, cmdline));
            }
        }
      else if (type == SYSPROF_CAPTURE_FRAME_ALLOCATION)
        {
          const SysprofCaptureAllocation *ev;
          Alloc a;

          if (!(ev = sysprof_capture_reader_read_allocation (g->reader)))
            break;

          frame_num++;

          /* Short-circuit if we don't care about this frame */
          if (!sysprof_selection_contains (g->selection, ev->frame.time))
            continue;

          a.pid = ev->frame.pid;
          a.tid = ev->tid;
          a.time = ev->frame.time;
          a.addr = ev->alloc_addr;
          a.size = ev->alloc_size;
          a.frame_num = frame_num;

          g_array_append_val (all_allocs, a);
        }
      else
        {
          if (!sysprof_capture_reader_skip (g->reader))
            break;
        }
    }

  /* Ensure items are in order because threads may be writing
   * data into larger buffers, which are flushed in whole by
   * the event marshalling from control fds.
   */
  g_array_sort (all_allocs, compare_alloc);

  for (guint i = 0; i < all_allocs->len; i++)
    {
      const Alloc *a = &g_array_index (all_allocs, Alloc, i);

      if (a->size <= 0)
        {
          if (a->addr == last_addr && last_addr)
            {
              const Alloc *prev = a - 1;
              g_array_append_vals (temp_allocs, prev, 1);
            }

          last_addr = 0;
        }
      else
        {
          last_addr = a->addr;
        }
    }

  if (temp_allocs->len == 0)
    return;

  /* Now sort by frame number so we can walk the reader and get the stack
   * for each allocation as we count frames.  We can skip frames until we
   * get to the matching frame_num for the next alloc.
   *
   * We sort in reverse so that we can just keep shortening the array as
   * we match each frame to save having to keep a secondary position
   * variable.
   */
  g_array_sort (temp_allocs, compare_frame_num_reverse);
  sysprof_capture_reader_reset (g->reader);
  frame_num = 0;
  while (sysprof_capture_reader_peek_type (g->reader, &type))
    {
      if (type == SYSPROF_CAPTURE_FRAME_ALLOCATION)
        {
          const SysprofCaptureAllocation *ev;
          const Alloc *tail;

          if (!(ev = sysprof_capture_reader_read_allocation (g->reader)))
            break;

          frame_num++;

          tail = &g_array_index (temp_allocs, Alloc, temp_allocs->len - 1);

          if G_UNLIKELY (tail->frame_num == frame_num)
            {
              SysprofAddressContext last_context = SYSPROF_ADDRESS_CONTEXT_NONE;
              const gchar *cmdline;
              guint len = 5;

              node = stack_stash_add_trace (g->building, ev->addrs, ev->n_addrs, ev->alloc_size);

              for (const StackNode *iter = node; iter != NULL; iter = iter->parent)
                len++;

              if (G_UNLIKELY (g->resolved->len < len))
                g_array_set_size (g->resolved, len);

              len = 0;

              for (const StackNode *iter = node; iter != NULL; iter = iter->parent)
                {
                  SysprofAddressContext context = SYSPROF_ADDRESS_CONTEXT_NONE;
                  SysprofAddress address = iter->data;
                  const gchar *symbol = NULL;

                  if (sysprof_address_is_context_switch (address, &context))
                    {
                      if (last_context)
                        symbol = sysprof_address_context_to_string (last_context);
                      else
                        symbol = NULL;

                      last_context = context;
                    }
                  else
                    {
                      for (guint i = 0; i < g->resolvers->len; i++)
                        {
                          SysprofSymbolResolver *resolver = g_ptr_array_index (g->resolvers, i);
                          GQuark tag = 0;
                          gchar *str;

                          str = sysprof_symbol_resolver_resolve_with_context (resolver,
                                                                              ev->frame.time,
                                                                              ev->frame.pid,
                                                                              last_context,
                                                                              address,
                                                                              &tag);

                          if (str != NULL)
                            {
                              symbol = g_string_chunk_insert_const (g->symbols, str);
                              g_free (str);

                              if (tag != 0 && !g_hash_table_contains (g->tags, symbol))
                                g_hash_table_insert (g->tags, (gchar *)symbol, GSIZE_TO_POINTER (tag));

                              break;
                            }
                        }
                    }

                  if (symbol != NULL)
                    g_array_index (g->resolved, SysprofAddress, len++) = POINTER_TO_U64 (symbol);
                }

              if ((cmdline = g_hash_table_lookup (g->cmdlines, GINT_TO_POINTER (ev->frame.pid))))
                g_array_index (g->resolved, guint64, len++) = POINTER_TO_U64 (cmdline);

              g_array_index (g->resolved, guint64, len++) = POINTER_TO_U64 ("[Everything]");

              stack_stash_add_trace (g->stash,
                                     (gpointer)g->resolved->data,
                                     len,
                                     ev->alloc_size);

              g_array_set_size (temp_allocs, temp_allocs->len - 1);

              if (temp_allocs->len == 0)
                break;
            }
        }
      else
        {
          if (!sysprof_capture_reader_skip (g->reader))
            break;
        }
    }
}

static void
sysprof_memprof_profile_generate_worker (GTask        *task,
                                         gpointer      source_object,
                                         gpointer      task_data,
                                         GCancellable *cancellable)
{
  Generate *g = task_data;

  g_assert (G_IS_TASK (task));
  g_assert (g != NULL);
  g_assert (g->reader != NULL);
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  /* Make sure the capture is at the beginning */
  sysprof_capture_reader_reset (g->reader);

  /* Load all our symbol resolvers */
  for (guint i = 0; i < g->resolvers->len; i++)
    {
      SysprofSymbolResolver *resolver = g_ptr_array_index (g->resolvers, i);

      sysprof_symbol_resolver_load (resolver, g->reader);
      sysprof_capture_reader_reset (g->reader);
    }

  if (g->mode == SYSPROF_MEMPROF_MODE_ALL_ALLOCS)
    {
      g_autoptr(SysprofCaptureCursor) cursor = NULL;

      cursor = create_cursor (g->reader);
      sysprof_capture_cursor_foreach (cursor, all_allocs_foreach_cb, g);
    }
  else if (g->mode == SYSPROF_MEMPROF_MODE_TEMP_ALLOCS)
    {
      temp_allocs_worker (g);
    }
  else if (g->mode == SYSPROF_MEMPROF_MODE_SUMMARY)
    {
      summary_worker (g);
    }

  /* Release some data we don't need anymore */
  g_clear_pointer (&g->resolved, g_array_unref);
  g_clear_pointer (&g->resolvers, g_ptr_array_unref);
  g_clear_pointer (&g->reader, sysprof_capture_reader_unref);
  g_clear_pointer (&g->building, stack_stash_unref);
  g_clear_pointer (&g->cmdlines, g_hash_table_unref);
  g_clear_object (&g->selection);

  g_task_return_boolean (task, TRUE);
}

static void
sysprof_memprof_profile_generate (SysprofProfile      *profile,
                                  GCancellable        *cancellable,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
  SysprofMemprofProfile *self = (SysprofMemprofProfile *)profile;
  g_autoptr(GTask) task = NULL;
  Generate *g;

  g_assert (SYSPROF_IS_MEMPROF_PROFILE (self));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_memprof_profile_generate);

  if (self->reader == NULL)
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_NOT_INITIALIZED,
                               "No capture reader has been set");
      return;
    }

  g = g_slice_new0 (Generate);
  g->ref_count = 1;
  g->reader = sysprof_capture_reader_copy (self->reader);
  g->selection = sysprof_selection_copy (self->selection);
  g->cmdlines = g_hash_table_new (NULL, NULL);
  g->rax = raxNew ();
  g->stash = stack_stash_new (NULL);
  g->building = stack_stash_new (NULL);
  g->resolvers = g_ptr_array_new_with_free_func (g_object_unref);
  g->symbols = g_string_chunk_new (4096*4);
  g->tags = g_hash_table_new (g_str_hash, g_str_equal);
  g->resolved = g_array_new (FALSE, TRUE, sizeof (guint64));
  g->mode = self->mode;

  g_ptr_array_add (g->resolvers, sysprof_capture_symbol_resolver_new ());
  g_ptr_array_add (g->resolvers, sysprof_kernel_symbol_resolver_new ());
  g_ptr_array_add (g->resolvers, sysprof_elf_symbol_resolver_new ());

  g_task_set_task_data (task, g, (GDestroyNotify) generate_unref);
  g_task_run_in_thread (task, sysprof_memprof_profile_generate_worker);
}

static gboolean
sysprof_memprof_profile_generate_finish (SysprofProfile  *profile,
                                         GAsyncResult    *result,
                                         GError         **error)
{
  SysprofMemprofProfile *self = (SysprofMemprofProfile *)profile;

  g_assert (SYSPROF_IS_MEMPROF_PROFILE (self));
  g_assert (G_IS_TASK (result));

  g_clear_pointer (&self->g, generate_unref);

  if (g_task_propagate_boolean (G_TASK (result), error))
    {
      Generate *g = g_task_get_task_data (G_TASK (result));
      self->g = generate_ref (g);
      return TRUE;
    }

  return FALSE;
}

static void
profile_iface_init (SysprofProfileInterface *iface)
{
  iface->set_reader = sysprof_memprof_profile_set_reader;
  iface->generate = sysprof_memprof_profile_generate;
  iface->generate_finish = sysprof_memprof_profile_generate_finish;
}

gpointer
sysprof_memprof_profile_get_native (SysprofMemprofProfile *self)
{
  g_return_val_if_fail (SYSPROF_IS_MEMPROF_PROFILE (self), NULL);

  if (self->g != NULL)
    return self->g->rax;

  return NULL;
}

gpointer
sysprof_memprof_profile_get_stash (SysprofMemprofProfile *self)
{
  g_return_val_if_fail (SYSPROF_IS_MEMPROF_PROFILE (self), NULL);

  if (self->g != NULL)
    return self->g->stash;

  return NULL;
}

gboolean
sysprof_memprof_profile_is_empty (SysprofMemprofProfile *self)
{
  StackNode *root;

  g_return_val_if_fail (SYSPROF_IS_MEMPROF_PROFILE (self), FALSE);

  return (self->g == NULL ||
          self->g->stash == NULL ||
          !(root = stack_stash_get_root (self->g->stash)) ||
          !root->total);
}

GQuark
sysprof_memprof_profile_get_tag (SysprofMemprofProfile *self,
                                 const gchar          *symbol)
{
  g_return_val_if_fail (SYSPROF_IS_MEMPROF_PROFILE (self), 0);

  if (self->g != NULL)
    return GPOINTER_TO_SIZE (g_hash_table_lookup (self->g->tags, symbol));

  return 0;
}

SysprofProfile *
sysprof_memprof_profile_new_with_selection (SysprofSelection *selection)
{
  return g_object_new (SYSPROF_TYPE_MEMPROF_PROFILE,
                       "selection", selection,
                       NULL);
}

void
sysprof_memprof_profile_get_stats (SysprofMemprofProfile *self,
                                   SysprofMemprofStats   *stats)
{
  g_return_if_fail (SYSPROF_IS_MEMPROF_PROFILE (self));
  g_return_if_fail (stats != NULL);

  if (self->g != NULL)
    *stats = self->g->stats;
  else
    memset (stats, 0, sizeof *stats);
}
