/* sysprof-callgraph-profile.c
 *
 * Copyright 2016-2019 Christian Hergert <christian@hergert.me>
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

/* Sysprof -- Sampling, systemwide CPU profiler
 * Copyright 2009-2012 Soeren Sandmann and others
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <glib/gi18n.h>
#include <string.h>
#include <sysprof-capture.h>
#include <unistd.h>

#include "../stackstash.h"

#include "sysprof-capture-util-private.h"

#include "sysprof-callgraph-profile.h"
#include "sysprof-capture-reader.h"
#include "sysprof-capture-symbol-resolver.h"
#include "sysprof-elf-symbol-resolver.h"
#include "sysprof-jitmap-symbol-resolver.h"
#include "sysprof-kernel-symbol-resolver.h"
#include "sysprof-map-lookaside.h"
#include "sysprof-selection.h"

#define CHECK_CANCELLABLE_INTERVAL 100

struct _SysprofCallgraphProfile
{
  GObject                parent_instance;

  SysprofCaptureReader  *reader;
  SysprofSelection      *selection;
  StackStash            *stash;
  GStringChunk          *symbols;
  GHashTable            *tags;
};

typedef struct
{
  SysprofCaptureReader *reader;
  SysprofSelection     *selection;
} Generate;

static void profile_iface_init (SysprofProfileInterface *iface);

G_DEFINE_TYPE_EXTENDED (SysprofCallgraphProfile, sysprof_callgraph_profile, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (SYSPROF_TYPE_PROFILE, profile_iface_init))

enum {
  PROP_0,
  PROP_SELECTION,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

SysprofProfile *
sysprof_callgraph_profile_new (void)
{
  return g_object_new (SYSPROF_TYPE_CALLGRAPH_PROFILE, NULL);
}

SysprofProfile *
sysprof_callgraph_profile_new_with_selection (SysprofSelection *selection)
{
  return g_object_new (SYSPROF_TYPE_CALLGRAPH_PROFILE,
                       "selection", selection,
                       NULL);
}

static void
sysprof_callgraph_profile_finalize (GObject *object)
{
  SysprofCallgraphProfile *self = (SysprofCallgraphProfile *)object;

  g_clear_pointer (&self->symbols, g_string_chunk_free);
  g_clear_pointer (&self->stash, stack_stash_unref);
  g_clear_pointer (&self->reader, sysprof_capture_reader_unref);
  g_clear_pointer (&self->tags, g_hash_table_unref);
  g_clear_object (&self->selection);

  G_OBJECT_CLASS (sysprof_callgraph_profile_parent_class)->finalize (object);
}

static void
sysprof_callgraph_profile_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  SysprofCallgraphProfile *self = SYSPROF_CALLGRAPH_PROFILE (object);

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
sysprof_callgraph_profile_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  SysprofCallgraphProfile *self = SYSPROF_CALLGRAPH_PROFILE (object);

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
sysprof_callgraph_profile_class_init (SysprofCallgraphProfileClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_callgraph_profile_finalize;
  object_class->get_property = sysprof_callgraph_profile_get_property;
  object_class->set_property = sysprof_callgraph_profile_set_property;

  properties [PROP_SELECTION] =
    g_param_spec_object ("selection",
                         "Selection",
                         "The selection for filtering the callgraph",
                         SYSPROF_TYPE_SELECTION,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_callgraph_profile_init (SysprofCallgraphProfile *self)
{
  self->symbols = g_string_chunk_new (_sysprof_getpagesize ());
  self->tags = g_hash_table_new (g_str_hash, g_str_equal);
}

static void
sysprof_callgraph_profile_set_reader (SysprofProfile       *profile,
                                      SysprofCaptureReader *reader)
{
  SysprofCallgraphProfile *self = (SysprofCallgraphProfile *)profile;

  g_assert (SYSPROF_IS_CALLGRAPH_PROFILE (self));
  g_assert (reader != NULL);

  g_clear_pointer (&self->reader, sysprof_capture_reader_unref);
  self->reader = sysprof_capture_reader_ref (reader);
}

static const gchar *
sysprof_callgraph_profile_intern_string_take (SysprofCallgraphProfile *self,
                                              gchar                   *str)
{
  const gchar *ret;

  g_assert (SYSPROF_IS_CALLGRAPH_PROFILE (self));
  g_assert (str != NULL);

  ret = g_string_chunk_insert_const (self->symbols, str);
  g_free (str);
  return ret;
}

static const gchar *
sysprof_callgraph_profile_intern_string (SysprofCallgraphProfile *self,
                                         const gchar             *str)
{
  g_assert (SYSPROF_IS_CALLGRAPH_PROFILE (self));
  g_assert (str != NULL);

  return g_string_chunk_insert_const (self->symbols, str);
}

static void
sysprof_callgraph_profile_generate_worker (GTask        *task,
                                           gpointer      source_object,
                                           gpointer      task_data,
                                           GCancellable *cancellable)
{
  SysprofCallgraphProfile *self = source_object;
  Generate *gen = task_data;
  SysprofCaptureReader *reader;
  SysprofSelection *selection;
  g_autoptr(GArray) resolved = NULL;
  g_autoptr(GHashTable) maps_by_pid = NULL;
  g_autoptr(GHashTable) cmdlines = NULL;
  g_autoptr(GPtrArray) resolvers = NULL;
  SysprofCaptureFrameType type;
  StackStash *stash = NULL;
  StackStash *resolved_stash = NULL;
  guint count = 0;
  gboolean ret = FALSE;

  g_assert (G_IS_TASK (task));
  g_assert (gen != NULL);
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  reader = gen->reader;
  selection = gen->selection;

  maps_by_pid = g_hash_table_new_full (NULL, NULL, NULL, (GDestroyNotify)sysprof_map_lookaside_free);
  cmdlines = g_hash_table_new (NULL, NULL);

  stash = stack_stash_new (NULL);
  resolved_stash = stack_stash_new (NULL);

  resolvers = g_ptr_array_new_with_free_func (g_object_unref);
  g_ptr_array_add (resolvers, sysprof_capture_symbol_resolver_new ());
  g_ptr_array_add (resolvers, sysprof_kernel_symbol_resolver_new ());
  g_ptr_array_add (resolvers, sysprof_elf_symbol_resolver_new ());
  g_ptr_array_add (resolvers, sysprof_jitmap_symbol_resolver_new ());

  for (guint j = 0; j < resolvers->len; j++)
    {
      SysprofSymbolResolver *resolver = g_ptr_array_index (resolvers, j);

      sysprof_capture_reader_reset (reader);
      sysprof_symbol_resolver_load (resolver, reader);
    }

  sysprof_capture_reader_reset (reader);

  /*
   * The resolved pointer array is where we stash the names for the
   * instruction pointers to pass to the stash stack. All the strings
   * need to be deduplicated so that pointer comparison works as if we
   * did instruction-pointer comparison.
   */
  resolved = g_array_new (FALSE, TRUE, sizeof (guint64));

  while (sysprof_capture_reader_peek_type (reader, &type))
    {
      const SysprofCaptureProcess *pr;

      if (type != SYSPROF_CAPTURE_FRAME_PROCESS)
        {
          if (!sysprof_capture_reader_skip (reader))
            goto failure;
          continue;
        }

      if (NULL == (pr = sysprof_capture_reader_read_process (reader)))
        goto failure;

      if (!g_hash_table_contains (cmdlines, GINT_TO_POINTER (pr->frame.pid)))
        {
          g_autofree gchar *cmdline = g_strdup_printf ("[%s]", pr->cmdline);
          g_hash_table_insert (cmdlines,
                               GINT_TO_POINTER (pr->frame.pid),
                               (gchar *)sysprof_callgraph_profile_intern_string (self, cmdline));
        }
    }

  if (g_task_return_error_if_cancelled (task))
    goto cleanup;

  sysprof_capture_reader_reset (reader);

  /*
   * Walk through all of the sample events and resolve instruction-pointers
   * to symbol names by loading the particular map and extracting the symbol
   * name. If we wanted to support dynamic systems, we'd want to extend this
   * to parse information from captured data about the languages jit'd code.
   */
  while (sysprof_capture_reader_peek_type (reader, &type))
    {
      SysprofAddressContext last_context = SYSPROF_ADDRESS_CONTEXT_NONE;
      const SysprofCaptureSample *sample;
      StackNode *node;
      StackNode *iter;
      const gchar *cmdline;
      guint len = 5;

      if (type != SYSPROF_CAPTURE_FRAME_SAMPLE)
        {
          if (!sysprof_capture_reader_skip (reader))
            goto failure;
          continue;
        }

      if (++count == CHECK_CANCELLABLE_INTERVAL)
        {
          if (g_task_return_error_if_cancelled (task))
            goto cleanup;
        }

      if (NULL == (sample = sysprof_capture_reader_read_sample (reader)))
        goto failure;

      if (!sysprof_selection_contains (selection, sample->frame.time))
        continue;

      if (sample->n_addrs == 0)
        continue;

      cmdline = g_hash_table_lookup (cmdlines, GINT_TO_POINTER (sample->frame.pid));

      if (cmdline == NULL)
        {
          gchar *pidstr = g_strdup_printf ("[Process %d]", sample->frame.pid);
          g_hash_table_insert (cmdlines, GINT_TO_POINTER (sample->frame.pid), pidstr);
          cmdline = pidstr;
        }

#if 0
      /* This assertion appears to hold true, but since we're taking in
       * untrusted data from capture files, it's not safe to assume. But in
       * practice it is.
       */
      g_assert (sysprof_address_is_context_switch (sample->addrs[0], &last_context));
      last_context = SYSPROF_ADDRESS_CONTEXT_NONE;
#endif

      node = stack_stash_add_trace (stash, (gpointer)sample->addrs, sample->n_addrs, 1);

      for (iter = node; iter != NULL; iter = iter->parent)
        len++;

      if (G_UNLIKELY (resolved->len < len))
        g_array_set_size (resolved, len);

      len = 0;

      for (iter = node; iter != NULL; iter = iter->parent)
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
              /* In case we get plain backtraces that aren't coming from perf,
               * we might never get a context switch into user-space. This ensures
               * that we still get traces for things from backtrace().
               */
              if (last_context == SYSPROF_ADDRESS_CONTEXT_NONE)
                last_context = SYSPROF_ADDRESS_CONTEXT_USER;

              for (guint j = 0; j < resolvers->len; j++)
                {
                  SysprofSymbolResolver *resolver = g_ptr_array_index (resolvers, j);
                  GQuark tag = 0;
                  gchar *str;

                  str = sysprof_symbol_resolver_resolve_with_context (resolver,
                                                                      sample->frame.time,
                                                                      sample->frame.pid,
                                                                      last_context,
                                                                      address,
                                                                      &tag);

                  if (str != NULL)
                    {
                      symbol = sysprof_callgraph_profile_intern_string_take (self, str);
                      if (tag != 0)
                        g_hash_table_insert (self->tags, (gchar *)symbol, GSIZE_TO_POINTER (tag));
                      break;
                    }
                }
            }

          if (symbol != NULL)
            g_array_index (resolved, SysprofAddress, len++) = POINTER_TO_U64 (symbol);
        }

      if (last_context && last_context != SYSPROF_ADDRESS_CONTEXT_USER)
        {
          /* Kernel threads do not have a user part, so we end up here
           * without ever getting a user context. If this happens,
           * add the '- - kernel - - ' name, so that kernel threads
           * are properly blamed on the kernel
           */
          const gchar *name = sysprof_address_context_to_string (last_context);
          g_array_index (resolved, SysprofAddress, len++) = POINTER_TO_U64 (name);
        }

      g_array_index (resolved, guint64, len++) = POINTER_TO_U64 (cmdline);
      g_array_index (resolved, guint64, len++) = POINTER_TO_U64 ("[Everything]");

      stack_stash_add_trace (resolved_stash, (gpointer)resolved->data, len, 1);
    }

  ret = TRUE;

failure:

  if (ret == FALSE)
    g_task_return_new_error (task,
                             G_IO_ERROR,
                             G_IO_ERROR_FAILED,
                             "%s",
                             _("Sysprof was unable to generate a callgraph from the system capture."));
  else
    g_task_return_pointer (task, g_steal_pointer (&resolved_stash), (GDestroyNotify)stack_stash_unref);

cleanup:
  g_clear_pointer (&resolved_stash, stack_stash_unref);
  g_clear_pointer (&stash, stack_stash_unref);
}

static void
generate_free (Generate *generate)
{
  sysprof_capture_reader_unref (generate->reader);
  g_clear_object (&generate->selection);
  g_slice_free (Generate, generate);
}

static void
sysprof_callgraph_profile_generate (SysprofProfile      *profile,
                                    GCancellable        *cancellable,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
  SysprofCallgraphProfile *self = (SysprofCallgraphProfile *)profile;
  Generate *gen;

  g_autoptr(GTask) task = NULL;

  g_assert (SYSPROF_IS_CALLGRAPH_PROFILE (self));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  gen = g_slice_new0 (Generate);
  gen->reader = sysprof_capture_reader_copy (self->reader);
  gen->selection = sysprof_selection_copy (self->selection);

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_task_data (task, gen, (GDestroyNotify)generate_free);
  g_task_run_in_thread (task, sysprof_callgraph_profile_generate_worker);
}

static gboolean
sysprof_callgraph_profile_generate_finish (SysprofProfile  *profile,
                                           GAsyncResult    *result,
                                           GError         **error)
{
  SysprofCallgraphProfile *self = (SysprofCallgraphProfile *)profile;
  StackStash *stash;

  g_assert (SYSPROF_IS_CALLGRAPH_PROFILE (self));
  g_assert (G_IS_TASK (result));

  stash = g_task_propagate_pointer (G_TASK (result), error);

  if (stash != NULL)
    {
      if (stash != self->stash)
        {
          g_clear_pointer (&self->stash, stack_stash_unref);
          self->stash = g_steal_pointer (&stash);
        }

      g_clear_pointer (&stash, stack_stash_unref);

      return TRUE;
    }

  return FALSE;
}

static void
profile_iface_init (SysprofProfileInterface *iface)
{
  iface->generate = sysprof_callgraph_profile_generate;
  iface->generate_finish = sysprof_callgraph_profile_generate_finish;
  iface->set_reader = sysprof_callgraph_profile_set_reader;
}

gpointer
sysprof_callgraph_profile_get_stash (SysprofCallgraphProfile *self)
{
  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH_PROFILE (self), NULL);

  return self->stash;
}

gboolean
sysprof_callgraph_profile_is_empty (SysprofCallgraphProfile *self)
{
  StackNode *root;

  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH_PROFILE (self), FALSE);

  return (self->stash == NULL ||
          !(root = stack_stash_get_root (self->stash)) ||
          !root->total);
}

GQuark
sysprof_callgraph_profile_get_tag (SysprofCallgraphProfile *self,
                                   const gchar        *symbol)
{
  g_return_val_if_fail (SYSPROF_IS_CALLGRAPH_PROFILE (self), 0);

  return GPOINTER_TO_SIZE (g_hash_table_lookup (self->tags, symbol));
}
