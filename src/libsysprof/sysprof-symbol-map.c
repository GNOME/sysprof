/* sysprof-symbol-map.c
 *
 * Copyright 2019 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sysprof-symbol-map"

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sysprof-map-lookaside.h"
#include "sysprof-symbol-map.h"

/*
 * Because we can't rely on the address ranges of symbols from ELF files
 * or elsewhere, we have to duplicate a lot of entries when building this
 * so that we can resolve all of the corrent addresses.
 */

SYSPROF_ALIGNED_BEGIN(1)
typedef struct
{
  SysprofCaptureAddress addr_begin;
  SysprofCaptureAddress addr_end;
  guint32               pid;
  guint32               offset;
  guint32               tag_offset;
  guint32               padding;
} Decoded
SYSPROF_ALIGNED_END(1);

struct _SysprofSymbolMap
{
  /* For creating maps */
  GStringChunk *chunk;
  GHashTable   *lookasides;
  GPtrArray    *resolvers;
  GPtrArray    *samples;
  guint         resolved : 1;

  /* For reading maps */
  GMappedFile   *mapped;
  const Decoded *symbols;
  gsize          n_symbols;
  const gchar   *beginptr;
  const gchar   *endptr;
};

typedef struct
{
  SysprofCaptureAddress  addr;
  const gchar           *name;
  GQuark                 tag;
  guint32                pid;
} Element;

static void
element_free (Element *ele)
{
  g_slice_free (Element, ele);
}

static gint
element_compare (gconstpointer a,
                 gconstpointer b)
{
  const Element *aa = *(const Element **)a;
  const Element *bb = *(const Element **)b;

  if (aa->pid < bb->pid)
    return -1;

  if (aa->pid > bb->pid)
    return 1;

  if (aa->addr < bb->addr)
    return -1;

  if (aa->addr > bb->addr)
    return 1;

  return 0;
}

static guint
element_hash (gconstpointer data)
{
  const Element *ele = data;
  struct {
    guint32 a;
    guint32 b;
  } addr;

  memcpy (&addr, &ele->addr, sizeof addr);
  return addr.a ^ addr.b ^ ele->pid;
}

static gboolean
element_equal (gconstpointer a,
               gconstpointer b)
{
  const Element *aa = a;
  const Element *bb = b;

  return aa->pid == bb->pid && aa->addr == bb->addr;
}

SysprofSymbolMap *
sysprof_symbol_map_new (void)
{
  SysprofSymbolMap *self;

  self = g_slice_new0 (SysprofSymbolMap);
  self->samples = g_ptr_array_new_with_free_func ((GDestroyNotify) element_free);
  self->chunk = g_string_chunk_new (4096*16);
  self->resolvers = g_ptr_array_new_with_free_func (g_object_unref);
  self->lookasides = g_hash_table_new_full (NULL, NULL, NULL,
                                            (GDestroyNotify) sysprof_map_lookaside_free);

  return g_steal_pointer (&self);
}

void
sysprof_symbol_map_free (SysprofSymbolMap *self)
{
  g_clear_pointer (&self->lookasides, g_hash_table_unref);
  g_clear_pointer (&self->resolvers, g_ptr_array_unref);
  g_clear_pointer (&self->chunk, g_string_chunk_free);
  g_clear_pointer (&self->samples, g_ptr_array_unref);
  g_clear_pointer (&self->mapped, g_mapped_file_unref);
  self->beginptr = NULL;
  self->endptr = NULL;
  self->symbols = NULL;
  self->n_symbols = 0;
  g_slice_free (SysprofSymbolMap, self);
}

static gint
search_for_symbol_cb (gconstpointer a,
                      gconstpointer b)
{
  const Decoded *key = a;
  const Decoded *ele = b;

  if (key->pid < ele->pid)
    return -1;

  if (key->pid > ele->pid)
    return 1;

  g_assert (key->pid == ele->pid);

  if (key->addr_begin < ele->addr_begin)
    return -1;

  if (key->addr_begin > ele->addr_end)
    return 1;

  g_assert (key->addr_begin >= ele->addr_begin);
  g_assert (key->addr_end <= ele->addr_end);

  return 0;
}

const gchar *
sysprof_symbol_map_lookup (SysprofSymbolMap      *self,
                           gint64                 time,
                           gint32                 pid,
                           SysprofCaptureAddress  addr,
                           GQuark                *tag)
{
  const Decoded *ret;
  const Decoded key = {
    .addr_begin = addr,
    .addr_end = addr,
    .pid = pid,
    .offset = 0,
    .tag_offset = 0,
  };

  g_assert (self != NULL);

  if (tag != NULL)
    *tag = 0;

  ret = bsearch (&key,
                 self->symbols,
                 self->n_symbols,
                 sizeof *ret,
                 search_for_symbol_cb);

  if (ret == NULL || ret->offset == 0)
    return NULL;

  if (tag != NULL && ret->tag_offset > 0)
    {
      if (ret->tag_offset < (self->endptr - self->beginptr))
        *tag = g_quark_from_string (&self->beginptr[ret->tag_offset]);
    }

  if (ret->offset < (self->endptr - self->beginptr))
    return &self->beginptr[ret->offset];

  return NULL;
}

void
sysprof_symbol_map_add_resolver (SysprofSymbolMap      *self,
                                 SysprofSymbolResolver *resolver)
{
  g_assert (self != NULL);
  g_assert (SYSPROF_IS_SYMBOL_RESOLVER (resolver));

  g_ptr_array_add (self->resolvers, g_object_ref (resolver));
}

static gboolean
sysprof_symbol_map_do_alloc (SysprofSymbolMap     *self,
                             SysprofCaptureReader *reader,
                             GHashTable           *seen)
{
  const SysprofCaptureAllocation *ev;

  g_assert (self != NULL);
  g_assert (reader != NULL);
  g_assert (seen != NULL);

  if (!(ev = sysprof_capture_reader_read_allocation (reader)))
    return FALSE;

  for (guint i = 0; i < ev->n_addrs; i++)
    {
      SysprofCaptureAddress addr = ev->addrs[i];

      for (guint j = 0; j < self->resolvers->len; j++)
        {
          SysprofSymbolResolver *resolver = g_ptr_array_index (self->resolvers, j);
          g_autofree gchar *name = NULL;
          const gchar *cname;
          Element ele;
          GQuark tag = 0;

          name = sysprof_symbol_resolver_resolve_with_context (resolver,
                                                               ev->frame.time,
                                                               ev->frame.pid,
                                                               SYSPROF_ADDRESS_CONTEXT_USER,
                                                               addr,
                                                               &tag);

          if (name == NULL)
            continue;

          cname = g_string_chunk_insert_const (self->chunk, name);

          ele.addr = addr;
          ele.pid = ev->frame.pid;
          ele.name = cname;
          ele.tag = tag;

          if (!g_hash_table_contains (seen, &ele))
            {
              Element *cpy = g_slice_dup (Element, &ele);
              g_hash_table_add (seen, cpy);
              g_ptr_array_add (self->samples, cpy);
            }
        }
    }

  return TRUE;
}

static gboolean
sysprof_symbol_map_do_sample (SysprofSymbolMap        *self,
                              SysprofCaptureReader    *reader,
                              GHashTable              *seen)
{
  SysprofAddressContext last_context = SYSPROF_ADDRESS_CONTEXT_NONE;
  const SysprofCaptureSample *sample;

  g_assert (self != NULL);
  g_assert (reader != NULL);
  g_assert (seen != NULL);

  if (!(sample = sysprof_capture_reader_read_sample (reader)))
    return FALSE;

  for (guint i = 0; i < sample->n_addrs; i++)
    {
      SysprofCaptureAddress addr = sample->addrs[i];
      SysprofAddressContext context;

      if (sysprof_address_is_context_switch (addr, &context))
        {
          last_context = context;
          continue;
        }

      /* Handle backtrace() style backtraces with no context switch */
      if (last_context == SYSPROF_ADDRESS_CONTEXT_NONE)
        last_context = SYSPROF_ADDRESS_CONTEXT_USER;

      for (guint j = 0; j < self->resolvers->len; j++)
        {
          SysprofSymbolResolver *resolver = g_ptr_array_index (self->resolvers, j);
          g_autofree gchar *name = NULL;
          const gchar *cname;
          Element ele;
          GQuark tag = 0;

          name = sysprof_symbol_resolver_resolve_with_context (resolver,
                                                               sample->frame.time,
                                                               sample->frame.pid,
                                                               last_context,
                                                               addr,
                                                               &tag);

          if (name == NULL)
            continue;

          cname = g_string_chunk_insert_const (self->chunk, name);

          ele.addr = addr;
          ele.pid = sample->frame.pid;
          ele.name = cname;
          ele.tag = tag;

          if (!g_hash_table_contains (seen, &ele))
            {
              Element *cpy = g_slice_dup (Element, &ele);
              g_hash_table_add (seen, cpy);
              g_ptr_array_add (self->samples, cpy);
            }
        }
    }

  return TRUE;
}

void
sysprof_symbol_map_resolve (SysprofSymbolMap     *self,
                            SysprofCaptureReader *reader)
{
  g_autoptr(GHashTable) seen = NULL;
  SysprofCaptureFrameType type;

  g_return_if_fail (self != NULL);
  g_return_if_fail (self->resolved == FALSE);
  g_return_if_fail (reader != NULL);

  self->resolved = TRUE;

  seen = g_hash_table_new (element_hash, element_equal);

  sysprof_capture_reader_reset (reader);

  for (guint i = 0; i < self->resolvers->len; i++)
    {
      sysprof_symbol_resolver_load (g_ptr_array_index (self->resolvers, i), reader);
      sysprof_capture_reader_reset (reader);
    }

  while (sysprof_capture_reader_peek_type (reader, &type))
    {
      if (type == SYSPROF_CAPTURE_FRAME_SAMPLE)
        {
          if (!sysprof_symbol_map_do_sample (self, reader, seen))
            break;
          continue;
        }
      else if (type == SYSPROF_CAPTURE_FRAME_ALLOCATION)
        {
          if (!sysprof_symbol_map_do_alloc (self, reader, seen))
            break;
          continue;
        }

      if (!sysprof_capture_reader_skip (reader))
        break;
    }

  g_ptr_array_sort (self->samples, element_compare);
}

void
sysprof_symbol_map_printf (SysprofSymbolMap *self)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (self->samples != NULL);

  for (guint i = 0; i < self->samples->len; i++)
    {
      Element *ele = g_ptr_array_index (self->samples, i);

      if (ele->tag)
        g_print ("%-5d: %"G_GUINT64_FORMAT": %s [%s]\n", ele->pid, ele->addr, ele->name, g_quark_to_string (ele->tag));
      else
        g_print ("%-5d: %"G_GUINT64_FORMAT": %s\n", ele->pid, ele->addr, ele->name);
    }
}

static guint
get_string_offset (GByteArray  *ar,
                   GHashTable  *seen,
                   const gchar *str)
{
  gpointer ret;

  if (str == NULL)
    return 0;

  if G_UNLIKELY (!g_hash_table_lookup_extended (seen, str, NULL, &ret))
    {
      ret = GUINT_TO_POINTER (ar->len);
      g_byte_array_append (ar, (guint8 *)str, strlen (str) + 1);
      g_hash_table_insert (seen, (gpointer)str, ret);
    }

  return GPOINTER_TO_UINT (ret);
}

gboolean
sysprof_symbol_map_serialize (SysprofSymbolMap *self,
                              gint              fd)
{
  static const Decoded empty = {0};
  SysprofCaptureAddress begin = 0;
  g_autoptr(GByteArray) ar = NULL;
  g_autoptr(GHashTable) seen = NULL;
  g_autoptr(GArray) decoded = NULL;
  gsize offset;

  g_assert (self != NULL);
  g_assert (fd != -1);

  ar = g_byte_array_new ();
  seen = g_hash_table_new (NULL, NULL);
  decoded = g_array_new (FALSE, FALSE, sizeof (Decoded));

  /* Add some empty space to both give us non-zero offsets and also ensure
   * empty space between data.
   */
  g_byte_array_append (ar, (guint8 *)&empty, sizeof empty);

  for (guint i = 0; i < self->samples->len; i++)
    {
      Element *ele = g_ptr_array_index (self->samples, i);
      Decoded dec;

      if (begin == 0)
        begin = ele->addr;

      if ((i + 1) < self->samples->len)
        {
          Element *next = g_ptr_array_index (self->samples, i + 1);

          if (ele->pid == next->pid && ele->name == next->name)
            continue;
        }

      dec.padding = 0;
      dec.addr_begin = begin;
      dec.addr_end = ele->addr;
      dec.pid = ele->pid;
      dec.offset = get_string_offset (ar, seen, ele->name);

      g_assert (!dec.offset || g_strcmp0 (ele->name, (gchar *)&ar->data[dec.offset]) == 0);

      if (ele->tag)
        {
          const gchar *tagstr = g_quark_to_string (ele->tag);

          dec.tag_offset = get_string_offset (ar, seen, tagstr);
          g_assert (g_strcmp0 (tagstr, (gchar *)&ar->data[dec.tag_offset]) == 0);
        }
      else
        dec.tag_offset = 0;

      g_array_append_val (decoded, dec);

      begin = 0;
    }

  offset = sizeof (Decoded) * (gsize)decoded->len;

  for (guint i = 0; i < decoded->len; i++)
    {
      Decoded *dec = &g_array_index (decoded, Decoded, i);

      if (dec->offset > 0)
        dec->offset += offset;

      if (dec->tag_offset > 0)
        dec->tag_offset += offset;
    }

  if (write (fd, decoded->data, offset) != offset)
    return FALSE;

  if (write (fd, ar->data, ar->len) != ar->len)
    return FALSE;

  /* Aggressively release state now that we're finished */
  if (self->samples->len)
    g_ptr_array_remove_range (self->samples, 0, self->samples->len);
  if (self->resolvers != NULL)
    g_ptr_array_remove_range (self->resolvers, 0, self->resolvers->len);
  g_string_chunk_clear (self->chunk);
  g_hash_table_remove_all (self->lookasides);

  lseek (fd, 0L, SEEK_SET);

  return TRUE;
}

gboolean
sysprof_symbol_map_deserialize (SysprofSymbolMap *self,
                                gint              byte_order,
                                gint              fd)
{
  g_autoptr(GError) error = NULL;
  gboolean needs_swap = byte_order != G_BYTE_ORDER;
  gchar *beginptr;
  gchar *endptr;

  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (self->mapped == NULL, FALSE);

  if (!(self->mapped = g_mapped_file_new_from_fd (fd, TRUE, &error)))
    {
      g_warning ("Failed to map file: %s\n", error->message);
      return FALSE;
    }

  beginptr = g_mapped_file_get_contents (self->mapped);
  endptr = beginptr + g_mapped_file_get_length (self->mapped);

  /* Ensure trialing \0 */
  if (endptr > beginptr)
    *(endptr - 1) = 0;

  for (gchar *ptr = beginptr;
       ptr < endptr && (ptr + sizeof (Decoded)) < endptr;
       ptr += sizeof (Decoded))
    {
      Decoded *sym = (Decoded *)ptr;

      if (sym->addr_begin == 0 &&
          sym->addr_end == 0 &&
          sym->pid == 0 &&
          sym->offset == 0)
        {
          self->symbols = (const Decoded *)beginptr;
          self->n_symbols = sym - self->symbols;
          break;
        }
      else if (needs_swap)
        {
          sym->addr_begin = GUINT64_SWAP_LE_BE (sym->addr_begin);
          sym->addr_end = GUINT64_SWAP_LE_BE (sym->addr_end);
          sym->pid = GUINT32_SWAP_LE_BE (sym->pid);
          sym->offset = GUINT32_SWAP_LE_BE (sym->offset);
          sym->tag_offset = GUINT32_SWAP_LE_BE (sym->tag_offset);
        }

#if 0
      g_print ("Added pid=%d  begin=%p  end=%p\n",
               sym->pid, (gpointer)sym->addr_begin, (gpointer)sym->addr_end);
#endif
    }

  self->beginptr = beginptr;
  self->endptr = endptr;

  return TRUE;
}
