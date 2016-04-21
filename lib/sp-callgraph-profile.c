/* sp-callgraph-profile.c
 *
 * Copyright (C) 2016 Christian Hergert <christian@hergert.me>
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

#include <glib/gi18n.h>
#include <string.h>
#include <unistd.h>

#include "sp-address.h"
#include "sp-callgraph-profile.h"
#include "sp-callgraph-profile-private.h"
#include "sp-capture-reader.h"
#include "sp-elf-symbol-resolver.h"
#include "sp-jitmap-symbol-resolver.h"
#include "sp-map-lookaside.h"
#include "sp-kernel-symbol-resolver.h"

#include "stackstash.h"

struct _SpCallgraphProfile
{
  GObject          parent_instance;

  SpCaptureReader *reader;
  GStringChunk    *symbols;
  StackStash      *stash;
  GHashTable      *tags;
};

static void profile_iface_init (SpProfileInterface *iface);

G_DEFINE_TYPE_EXTENDED (SpCallgraphProfile, sp_callgraph_profile, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (SP_TYPE_PROFILE, profile_iface_init))

SpProfile *
sp_callgraph_profile_new (void)
{
  return g_object_new (SP_TYPE_CALLGRAPH_PROFILE, NULL);
}

static void
sp_callgraph_profile_finalize (GObject *object)
{
  SpCallgraphProfile *self = (SpCallgraphProfile *)object;

  g_clear_pointer (&self->symbols, g_string_chunk_free);
  g_clear_pointer (&self->stash, stack_stash_unref);
  g_clear_pointer (&self->reader, sp_capture_reader_unref);
  g_clear_pointer (&self->tags, g_hash_table_unref);

  G_OBJECT_CLASS (sp_callgraph_profile_parent_class)->finalize (object);
}

static void
sp_callgraph_profile_class_init (SpCallgraphProfileClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sp_callgraph_profile_finalize;
}

static void
sp_callgraph_profile_init (SpCallgraphProfile *self)
{
  self->symbols = g_string_chunk_new (getpagesize ());
  self->tags = g_hash_table_new (g_str_hash, g_str_equal);
}

static void
sp_callgraph_profile_set_reader (SpProfile       *profile,
                                 SpCaptureReader *reader)
{
  SpCallgraphProfile *self = (SpCallgraphProfile *)profile;

  g_assert (SP_IS_CALLGRAPH_PROFILE (self));
  g_assert (reader != NULL);

  g_clear_pointer (&self->reader, sp_capture_reader_unref);
  self->reader = sp_capture_reader_ref (reader);
}

static const gchar *
sp_callgraph_profile_intern_string_take (SpCallgraphProfile *self,
                                         gchar              *str)
{
  const gchar *ret;

  g_assert (SP_IS_CALLGRAPH_PROFILE (self));
  g_assert (str != NULL);

  ret = g_string_chunk_insert_const (self->symbols, str);
  g_free (str);
  return ret;
}

static const gchar *
sp_callgraph_profile_intern_string (SpCallgraphProfile *self,
                                    const gchar        *str)
{
  g_assert (SP_IS_CALLGRAPH_PROFILE (self));
  g_assert (str != NULL);

  return g_string_chunk_insert_const (self->symbols, str);
}

static void
sp_callgraph_profile_generate_worker (GTask        *task,
                                      gpointer      source_object,
                                      gpointer      task_data,
                                      GCancellable *cancellable)
{
  SpCallgraphProfile *self = source_object;
  SpCaptureReader *reader = task_data;
  g_autoptr(GArray) resolved = NULL;
  g_autoptr(GHashTable) maps_by_pid = NULL;
  g_autoptr(GHashTable) cmdlines = NULL;
  g_autoptr(GPtrArray) resolvers = NULL;
  SpCaptureFrameType type;
  StackStash *stash;
  StackStash *resolved_stash;
  gboolean ret = FALSE;
  guint j;

  g_assert (G_IS_TASK (task));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  maps_by_pid = g_hash_table_new_full (NULL, NULL, NULL, (GDestroyNotify)sp_map_lookaside_free);
  cmdlines = g_hash_table_new (NULL, NULL);

  stash = stack_stash_new (NULL);
  resolved_stash = stack_stash_new (NULL);

  resolvers = g_ptr_array_new_with_free_func (g_object_unref);
  g_ptr_array_add (resolvers, sp_kernel_symbol_resolver_new ());
  g_ptr_array_add (resolvers, sp_elf_symbol_resolver_new ());
  g_ptr_array_add (resolvers, sp_jitmap_symbol_resolver_new ());

  for (j = 0; j < resolvers->len; j++)
    {
      SpSymbolResolver *resolver = g_ptr_array_index (resolvers, j);

      sp_capture_reader_reset (reader);
      sp_symbol_resolver_load (resolver, reader);
    }

  sp_capture_reader_reset (reader);

  /*
   * The resolved pointer array is where we stash the names for the
   * instruction pointers to pass to the stash stack. All the strings
   * need to be deduplicated so that pointer comparison works as if we
   * did instruction-pointer comparison.
   */
  resolved = g_array_new (FALSE, TRUE, sizeof (guint64));

  while (sp_capture_reader_peek_type (reader, &type))
    {
      const SpCaptureProcess *pr;
      const gchar *cmdline;

      if (type != SP_CAPTURE_FRAME_PROCESS)
        {
          if (!sp_capture_reader_skip (reader))
            goto failure;
          continue;
        }

      if (NULL == (pr = sp_capture_reader_read_process (reader)))
        goto failure;

      cmdline = g_strdup_printf ("[%s]", pr->cmdline);
      g_hash_table_insert (cmdlines,
                           GINT_TO_POINTER (pr->frame.pid),
                           (gchar *)sp_callgraph_profile_intern_string (self, cmdline));
    }

  sp_capture_reader_reset (reader);

  /*
   * Walk through all of the sample events and resolve instruction-pointers
   * to symbol names by loading the particular map and extracting the symbol
   * name. If we wanted to support dynamic systems, we'd want to extend this
   * to parse information from captured data about the languages jit'd code.
   */
  while (sp_capture_reader_peek_type (reader, &type))
    {
      SpAddressContext last_context = SP_ADDRESS_CONTEXT_NONE;
      const SpCaptureSample *sample;
      StackNode *node;
      StackNode *iter;
      const gchar *cmdline;
      guint len = 5;

      if (type != SP_CAPTURE_FRAME_SAMPLE)
        {
          if (!sp_capture_reader_skip (reader))
            goto failure;
          continue;
        }

      if (NULL == (sample = sp_capture_reader_read_sample (reader)))
        goto failure;

      cmdline = g_hash_table_lookup (cmdlines, GINT_TO_POINTER (sample->frame.pid));

      node = stack_stash_add_trace (stash, sample->addrs, sample->n_addrs, 1);

      for (iter = node; iter != NULL; iter = iter->parent)
        len++;

      if (G_UNLIKELY (resolved->len < len))
        g_array_set_size (resolved, len);

      len = 0;

      for (iter = node; iter != NULL; iter = iter->parent)
        {
          SpAddressContext context = SP_ADDRESS_CONTEXT_NONE;
          SpAddress address = iter->data;
          const gchar *symbol = NULL;

          if (sp_address_is_context_switch (address, &context))
            {
              if (last_context)
                symbol = sp_address_context_to_string (last_context);
              else
                symbol = NULL;

              last_context = context;
            }
          else
            {
              for (j = 0; j < resolvers->len; j++)
                {
                  SpSymbolResolver *resolver = g_ptr_array_index (resolvers, j);
                  GQuark tag = 0;
                  gchar *str;

                  str = sp_symbol_resolver_resolve (resolver,
                                                    sample->frame.time,
                                                    sample->frame.pid,
                                                    address,
                                                    &tag);

                  if (str != NULL)
                    {
                      symbol = sp_callgraph_profile_intern_string_take (self, str);
                      if (tag != 0)
                        g_hash_table_insert (self->tags, (gchar *)symbol, GSIZE_TO_POINTER (tag));
                      break;
                    }
                }
            }

          if (symbol != NULL)
            g_array_index (resolved, SpAddress, len++) = POINTER_TO_U64 (symbol);
        }

      if (last_context && last_context != SP_ADDRESS_CONTEXT_USER)
        {
          /* Kernel threads do not have a user part, so we end up here
           * without ever getting a user context. If this happens,
           * add the '- - kernel - - ' name, so that kernel threads
           * are properly blamed on the kernel
           */
          const gchar *name = sp_address_context_to_string (last_context);
          g_array_index (resolved, SpAddress, len++) = POINTER_TO_U64 (name);
        }

      if (cmdline != NULL)
        g_array_index (resolved, guint64, len++) = POINTER_TO_U64 (cmdline);

      g_array_index (resolved, guint64, len++) = POINTER_TO_U64 ("[Everything]");

      stack_stash_add_trace (resolved_stash, (SpAddress *)(gpointer)resolved->data, len, 1);
    }

  ret = TRUE;

failure:

  if (ret == FALSE)
    g_task_return_new_error (task,
                             G_IO_ERROR,
                             G_IO_ERROR_FAILED,
                             _("Sysprof was unable to generate a callgraph from the system capture."));
  self->stash = resolved_stash;
  stack_stash_unref (stash);
  g_task_return_boolean (task, ret);
}

static void
sp_callgraph_profile_generate (SpProfile           *profile,
                               GCancellable        *cancellable,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
  SpCallgraphProfile *self = (SpCallgraphProfile *)profile;

  g_autoptr(GTask) task = NULL;

  g_assert (SP_IS_CALLGRAPH_PROFILE (self));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_task_data (task, self->reader, NULL);
  g_task_run_in_thread (task, sp_callgraph_profile_generate_worker);
}

static gboolean
sp_callgraph_profile_generate_finish (SpProfile     *profile,
                                      GAsyncResult  *result,
                                      GError       **error)
{
  g_assert (SP_IS_CALLGRAPH_PROFILE (profile));
  g_assert (G_IS_TASK (result));

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
profile_iface_init (SpProfileInterface *iface)
{
  iface->generate = sp_callgraph_profile_generate;
  iface->generate_finish = sp_callgraph_profile_generate_finish;
  iface->set_reader = sp_callgraph_profile_set_reader;
}

StackStash *
sp_callgraph_profile_get_stash (SpCallgraphProfile *self)
{
  g_return_val_if_fail (SP_IS_CALLGRAPH_PROFILE (self), NULL);

  return self->stash;
}

GQuark
sp_callgraph_profile_get_tag (SpCallgraphProfile *self,
                              const gchar        *symbol)
{
  g_return_val_if_fail (SP_IS_CALLGRAPH_PROFILE (self), 0);

  return GPOINTER_TO_SIZE (g_hash_table_lookup (self->tags, symbol));
}
