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

#include "sysprof-capture-symbol-resolver.h"
#include "sysprof-elf-symbol-resolver.h"
#include "sysprof-kernel-symbol-resolver.h"
#include "sysprof-memprof-profile.h"
#include "sysprof-symbol-resolver.h"

#include "rax.h"
#include "../stackstash.h"

typedef struct
{
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
} Generate;

struct _SysprofMemprofProfile
{
  GObject               parent_instance;
  SysprofSelection     *selection;
  SysprofCaptureReader *reader;
  Generate             *g;
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
generate_free (Generate *g)
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
}

static Generate *
generate_ref (Generate *g)
{
  return g_atomic_rc_box_acquire (g);
}

static void
generate_unref (Generate *g)
{
  g_atomic_rc_box_release_full (g, (GDestroyNotify)generate_free);
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

static gboolean
cursor_foreach_cb (const SysprofCaptureFrame *frame,
                   gpointer                   user_data)
{
  Generate *g = user_data;

  g_assert (frame != NULL);
  g_assert (frame->type == SYSPROF_CAPTURE_FRAME_ALLOCATION ||
            frame->type == SYSPROF_CAPTURE_FRAME_PROCESS);

  if G_UNLIKELY (frame->type == SYSPROF_CAPTURE_FRAME_PROCESS)
    {
      const SysprofCaptureProcess *pr = (const SysprofCaptureProcess *)frame;
      g_autofree gchar *cmdline = g_strdup_printf ("[%s]", pr->cmdline);

      g_hash_table_insert (g->cmdlines,
                           GINT_TO_POINTER (frame->pid),
                           (gchar *)g_string_chunk_insert_const (g->symbols, cmdline));

      return TRUE;
    }

  /* Short-circuit if we don't care about this frame */
  if (!sysprof_selection_contains (g->selection, frame->time))
    return TRUE;

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

  return TRUE;
}

static void
sysprof_memprof_profile_generate_worker (GTask        *task,
                                         gpointer      source_object,
                                         gpointer      task_data,
                                         GCancellable *cancellable)
{
  SysprofCaptureCursor *cursor;
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

  cursor = create_cursor (g->reader);
  sysprof_capture_cursor_foreach (cursor, cursor_foreach_cb, g);

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

  g = g_atomic_rc_box_new0 (Generate);
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
