/* sysprof-kernel-symbol.c
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sysprof-kernel-symbol"

#include "config.h"

#include <gio/gio.h>
#include <sysprof-capture.h>

#include "sysprof-helpers.h"
#include "sysprof-kallsyms.h"
#include "sysprof-kernel-symbol.h"
#include "sysprof-private.h"

static G_LOCK_DEFINE (kernel_lock);
static GStringChunk *kernel_symbol_strs;
static GHashTable   *kernel_symbols_skip_hash;
static const gchar  *kernel_symbols_skip[] = {
  /* IRQ stack */
  "common_interrupt",
  "apic_timer_interrupt",
  "smp_apic_timer_interrupt",
  "hrtimer_interrupt",
  "__run_hrtimer",
  "perf_swevent_hrtimer",
  "perf_event_overflow",
  "__perf_event_overflow",
  "perf_prepare_sample",
  "perf_callchain",
  "perf_swcounter_hrtimer",
  "perf_counter_overflow",
  "__perf_counter_overflow",
  "perf_counter_output",

  /* NMI stack */
  "nmi_stack_correct",
  "do_nmi",
  "notify_die",
  "atomic_notifier_call_chain",
  "notifier_call_chain",
  "perf_event_nmi_handler",
  "perf_counter_nmi_handler",
  "intel_pmu_handle_irq",
  "perf_event_overflow",
  "perf_counter_overflow",
  "__perf_event_overflow",
  "perf_prepare_sample",
  "perf_callchain",
};

static inline gboolean
type_is_ignored (guint8 type)
{
  /* Only allow symbols in the text (code) section */
  return (type != 't' && type != 'T');
}

static gint
sysprof_kernel_symbol_compare (gconstpointer a,
                               gconstpointer b)
{
  const SysprofKernelSymbol *syma = a;
  const SysprofKernelSymbol *symb = b;

  if (syma->address > symb->address)
    return 1;
  else if (syma->address == symb->address)
    return 0;
  else
    return -1;
}

static void
do_shared_init (void)
{
  static gsize once;
  
  if (g_once_init_enter (&once))
    {
      g_autoptr(GHashTable) skip = NULL;

      kernel_symbol_strs = g_string_chunk_new (4096 * 4);

      skip = g_hash_table_new (g_str_hash, g_str_equal);
      for (guint i = 0; i < G_N_ELEMENTS (kernel_symbols_skip); i++)
        g_hash_table_insert (skip, (gchar *)kernel_symbols_skip[i], NULL);
      kernel_symbols_skip_hash = g_steal_pointer (&skip);

      g_once_init_leave (&once, TRUE);
    }
}

SysprofKernelSymbols *
_sysprof_kernel_symbols_new_from_kallsyms (SysprofKallsyms *kallsyms)
{
  static const SysprofKernelSymbol empty = {0};
  SysprofKernelSymbols *self;
  const gchar *name;
  guint64 addr;
  guint8 type;

  do_shared_init ();

  g_return_val_if_fail (kallsyms != NULL, NULL);

  self = g_array_new (FALSE, FALSE, sizeof (SysprofKernelSymbol));

  G_LOCK (kernel_lock);

  while (sysprof_kallsyms_next (kallsyms, &name, &addr, &type))
    {
      if (!type_is_ignored (type))
        {
          SysprofKernelSymbol sym;

          sym.address = addr;
          sym.name = g_string_chunk_insert_const (kernel_symbol_strs, name);

          g_array_append_val (self, sym);
        }
    }

  g_array_sort (self, sysprof_kernel_symbol_compare);

  /* Always add a trailing node */
  g_array_append_val (self, empty);

  G_UNLOCK (kernel_lock);

  return g_steal_pointer (&self);
}

SysprofKernelSymbols *
_sysprof_kernel_symbols_get_shared (void)
{
  static SysprofKernelSymbols *shared;
  static SysprofKernelSymbols empty[] = { 0 };

  if (shared == NULL)
    {
#ifdef __linux__
      SysprofHelpers *helpers = sysprof_helpers_get_default ();
      g_autofree gchar *contents = NULL;

      if (sysprof_helpers_get_proc_file (helpers, "/proc/kallsyms", NULL, &contents, NULL))
        {
          g_autoptr(SysprofKallsyms) kallsyms = sysprof_kallsyms_new_take (g_steal_pointer (&contents));
          shared = _sysprof_kernel_symbols_new_from_kallsyms (kallsyms);
        }
#endif

      if (shared == NULL)
        shared = empty;
    }

  return shared;
}

static const SysprofKernelSymbol *
sysprof_kernel_symbol_lookup (SysprofKernelSymbol   *symbols,
                              SysprofCaptureAddress  address,
                              guint                  first,
                              guint                  last)
{
  if (symbols == NULL)
    return NULL;

  if (address >= symbols [last].address)
    {
      return &symbols [last];
    }
  else if (last - first < 3)
    {
      while (last >= first)
        {
          if (address >= symbols[last].address)
            return &symbols [last];

          last--;
        }

      return NULL;
    }
  else
    {
      int mid = (first + last) / 2;

      if (symbols [mid].address > address)
        return sysprof_kernel_symbol_lookup (symbols, address, first, mid);
      else
        return sysprof_kernel_symbol_lookup (symbols, address, mid, last);
    }
}

/*
 * sysprof_kernel_symbols_lookup:
 * @self: the symbol data to lookup
 * @address: the address of the instruction pointer
 *
 * Locates the kernel symbol that contains @address.
 *
 * Returns: (transfer none): An #SysprofKernelSymbol or %NULL.
 */
const SysprofKernelSymbol *
_sysprof_kernel_symbols_lookup (const SysprofKernelSymbols *self,
                                SysprofCaptureAddress       address)
{
  const SysprofKernelSymbol *first;
  const SysprofKernelSymbol *ret;

  g_assert (self != NULL);

  if (self->len < 2)
    return NULL;

  /* Short circuit if this is out of range */
  first = &g_array_index (self, SysprofKernelSymbol, 0);
  if (address < first->address)
    return NULL;

  ret = sysprof_kernel_symbol_lookup ((SysprofKernelSymbol *)(gpointer)self->data,
                                      address,
                                      0,
                                      /* 1 for right-most, 1 for empty node */
                                      self->len - 2);

  /* We resolve all symbols, including ignored symbols so that we
   * don't give back the wrong function juxtapose an ignored func.
   */
  if (ret != NULL && g_hash_table_contains (kernel_symbols_skip_hash, ret->name))
    return NULL;

  return ret;
}
