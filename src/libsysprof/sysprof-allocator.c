/*
 * sysprof-allocator.c
 *
 * Copysysprofht 2024 Christian Hergert <chergert@redhat.com>
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

#include "sysprof-allocator-private.h"

#define SYSPROF_ALLOCATOR_MIN_PAGE_SIZE (4*4096)

typedef struct _SysprofPage
{
  struct _SysprofPage *next;
  guint8 *data;
  gsize avail;
  gsize len;
} SysprofPage;

struct _SysprofAllocator
{
  SysprofPage *pages;
  SysprofPage *current;
  GHashTable *strings;
  char empty[1];
};

static inline SysprofPage *
sysprof_page_new (gsize size)
{
  SysprofPage *page = g_new0 (SysprofPage, 1);

  page->len = size;
  page->data = g_malloc (size);
  page->avail = page->len;
  page->next = NULL;

  return page;
}

SysprofAllocator *
sysprof_allocator_new (void)
{
  SysprofAllocator *self;

  self = g_atomic_rc_box_new0 (SysprofAllocator);
  self->pages = sysprof_page_new (SYSPROF_ALLOCATOR_MIN_PAGE_SIZE);
  self->current = self->pages;
  self->strings = g_hash_table_new (g_str_hash, g_str_equal);

  return self;
}

SysprofAllocator *
sysprof_allocator_ref (SysprofAllocator *self)
{
  return g_atomic_rc_box_acquire (self);
}

static void
sysprof_allocator_finalize (gpointer data)
{
  SysprofAllocator *self = data;
  SysprofPage *page;

  self->current = NULL;

  g_clear_pointer (&self->strings, g_hash_table_unref);

  while ((page = self->pages))
    {
      self->pages = page->next;

      g_free (page->data);
      g_free (page);
    }

}

void
sysprof_allocator_unref (SysprofAllocator *self)
{
  g_atomic_rc_box_release_full (self, sysprof_allocator_finalize);
}

static inline gboolean
sysprof_allocator_ispow2 (gsize x)
{
  return ((x != 0) && !(x & (x - 1)));
}

static inline gsize
sysprof_allocator_nextpow2 (gsize x)
{
  return x == 1 ? 1 : 1 << (64 - __builtin_clzl (x - 1));
}

#define LOWER_MASK ((GLIB_SIZEOF_VOID_P*2)-1)

gpointer
sysprof_allocator_alloc (SysprofAllocator *allocator,
                         gsize             size)
{
  gpointer ret;

  if (size == 0)
    return NULL;

  if (size < 16)
    size = 16;
  else if (size & LOWER_MASK)
    size += (LOWER_MASK + 1) - (size & LOWER_MASK);

  g_assert ((size & LOWER_MASK) == 0);

  if G_UNLIKELY (size > allocator->current->avail)
    {
      SysprofPage *page;

      if (size > SYSPROF_ALLOCATOR_MIN_PAGE_SIZE)
        {
          gsize newsize = size;

          if (!sysprof_allocator_ispow2 (size))
            newsize = sysprof_allocator_nextpow2 (size);

          page = sysprof_page_new (newsize);
          page->avail = newsize - size;

          if (page->avail < allocator->current->avail)
            {
              page->next = allocator->pages;
              allocator->pages = page;
            }
          else
            {
              allocator->current->next = page;
              allocator->current = page;
            }

          return page->data;
        }
      else
        {
          page = sysprof_page_new (allocator->current->len * 2);
          allocator->current->next = page;
          allocator->current = page;
        }

    }

  g_assert (allocator->current->len >= allocator->current->avail);
  g_assert (size <= allocator->current->avail);

  ret = &allocator->current->data[allocator->current->len - allocator->current->avail];
  allocator->current->avail -= size;
  return ret;
}

gconstpointer
sysprof_allocator_cstring (SysprofAllocator *allocator,
                           const char       *str,
                           gssize            len)
{
  char *dst;

  if (len == 0 || str[0] == 0)
    return (&allocator->empty);

  if (len < 0)
    {
      if ((dst = g_hash_table_lookup (allocator->strings, str)))
        return dst;
      len = strlen (str);
    }
  else
    {
      char stack[128];

      if (len < G_N_ELEMENTS (stack))
        {
          memcpy (stack, str, len);
          stack[len] = 0;
          dst = g_hash_table_lookup (allocator->strings, stack);
        }
      else
        {
          char *freeme = g_strndup (str, len);
          dst = g_hash_table_lookup (allocator->strings, freeme);
          g_free (freeme);
        }

      if (dst != NULL)
        return dst;
    }

  dst = sysprof_allocator_alloc (allocator, len + 1);
  memcpy (dst, str, len);
  dst[len] = 0;


  g_hash_table_replace (allocator->strings, dst, dst);

  return dst;
}
