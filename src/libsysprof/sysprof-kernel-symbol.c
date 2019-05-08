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
#ifdef ENABLE_POLKIT
# include <polkit/polkit.h>
#endif
#include <sysprof-capture.h>

#include "sysprof-kallsyms.h"
#include "sysprof-kernel-symbol.h"

static GArray *kernel_symbols;
static GStringChunk *kernel_symbol_strs;
static GHashTable *kernel_symbols_skip_hash;
static const gchar *kernel_symbols_skip[] = {
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

static inline gboolean
type_is_ignored (guint8 type)
{
  /* Only allow symbols in the text (code) section */
  return (type != 't' && type != 'T');
}

static gboolean
authorize_proxy (GDBusConnection *conn)
{
#ifdef ENABLE_POLKIT
  PolkitSubject *subject = NULL;
  GPermission *permission = NULL;
  const gchar *name;

  g_assert (G_IS_DBUS_CONNECTION (conn));

  name = g_dbus_connection_get_unique_name (conn);
  if (name == NULL)
    goto failure;

  subject = polkit_system_bus_name_new (name);
  if (subject == NULL)
    goto failure;

  permission = polkit_permission_new_sync ("org.gnome.sysprof2.get-kernel-symbols", subject, NULL, NULL);
  if (permission == NULL)
    goto failure;

  if (!g_permission_acquire (permission, NULL, NULL))
    goto failure;

  return TRUE;

failure:
  g_clear_object (&subject);
  g_clear_object (&permission);
#endif

  return FALSE;
}

static gboolean
sysprof_kernel_symbol_load_from_sysprofd (void)
{
  g_autoptr(GDBusConnection) conn = NULL;
  g_autoptr(GVariant) ret = NULL;
  g_autoptr(GVariant) results = NULL;
  g_autoptr(GArray) ar = NULL;
  g_autoptr(GError) error = NULL;
  GVariantIter iter;
  const gchar *name;
  guint64 addr;
  guint8 type;

  if (!(conn = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL)))
    return FALSE;

  if (!authorize_proxy (conn))
    {
      g_warning ("Failed to acquire sufficient credentials to read kernel symbols");
      return FALSE;
    }

  ret = g_dbus_connection_call_sync (conn,
                                     "org.gnome.Sysprof2",
                                     "/org/gnome/Sysprof2",
                                     "org.gnome.Sysprof2",
                                     "GetKernelSymbols",
                                     NULL,
                                     G_VARIANT_TYPE ("(a(tys))"),
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL,
                                     &error);

  if (error != NULL)
    {
      g_warning ("Failed to load symbols from sysprofd: %s", error->message);
      return FALSE;
    }

  ar = g_array_new (FALSE, TRUE, sizeof (SysprofKernelSymbol));

  results = g_variant_get_child_value (ret, 0);
  g_variant_iter_init (&iter, results);
  while (g_variant_iter_loop (&iter, "(ty&s)", &addr, &type, &name))
    {
      SysprofKernelSymbol sym;

      if (type_is_ignored (type))
        continue;

      sym.address = addr;
      sym.name = g_string_chunk_insert_const (kernel_symbol_strs, name);

      g_array_append_val (ar, sym);
    }

  g_array_sort (ar, sysprof_kernel_symbol_compare);

#if 0
  g_print ("First: 0x%lx  Last: 0x%lx\n",
           g_array_index (ar, SysprofKernelSymbol, 0).address,
           g_array_index (ar, SysprofKernelSymbol, ar->len - 1).address);
#endif

  kernel_symbols = g_steal_pointer (&ar);

  return TRUE;
}

static gboolean
sysprof_kernel_symbol_load (void)
{
  g_autoptr(GHashTable) skip = NULL;
  g_autoptr(SysprofKallsyms) kallsyms = NULL;
  g_autoptr(GArray) ar = NULL;
  const gchar *name;
  guint64 addr;
  guint8 type;

  skip = g_hash_table_new (g_str_hash, g_str_equal);
  for (guint i = 0; i < G_N_ELEMENTS (kernel_symbols_skip); i++)
    g_hash_table_insert (skip, (gchar *)kernel_symbols_skip[i], NULL);
  kernel_symbols_skip_hash = g_steal_pointer (&skip);

  kernel_symbol_strs = g_string_chunk_new (4096);
  ar = g_array_new (FALSE, TRUE, sizeof (SysprofKernelSymbol));

  if (!(kallsyms = sysprof_kallsyms_new (NULL)))
    goto query_daemon;

  while (sysprof_kallsyms_next (kallsyms, &name, &addr, &type))
    {
      SysprofKernelSymbol sym;

      if (type_is_ignored (type))
        continue;

      sym.address = addr;
      sym.name = g_string_chunk_insert_const (kernel_symbol_strs, name);

      g_array_append_val (ar, sym);
    }

  if (ar->len == 0)
    goto query_daemon;

  g_array_sort (ar, sysprof_kernel_symbol_compare);
  kernel_symbols = g_steal_pointer (&ar);

  return TRUE;

query_daemon:
  if (sysprof_kernel_symbol_load_from_sysprofd ())
    return TRUE;

  g_warning ("Kernel symbols will not be available.");

  return FALSE;
}

static const SysprofKernelSymbol *
sysprof_kernel_symbol_lookup (SysprofKernelSymbol   *symbols,
                         SysprofCaptureAddress  address,
                         guint             first,
                         guint             last)
{
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

/**
 * sysprof_kernel_symbol_from_address:
 * @address: the address of the instruction pointer
 *
 * Locates the kernel symbol that contains @address.
 *
 * Returns: (transfer none): An #SysprofKernelSymbol or %NULL.
 */
const SysprofKernelSymbol *
sysprof_kernel_symbol_from_address (SysprofCaptureAddress address)
{
  const SysprofKernelSymbol *first;
  const SysprofKernelSymbol *ret;

  if G_UNLIKELY (kernel_symbols == NULL)
    {
      static gboolean failed;

      if (failed)
        return NULL;

      if (!sysprof_kernel_symbol_load ())
        {
          failed = TRUE;
          return NULL;
        }
    }

  g_assert (kernel_symbols != NULL);
  g_assert (kernel_symbols->len > 0);

  /* Short circuit if this is out of range */
  first = &g_array_index (kernel_symbols, SysprofKernelSymbol, 0);
  if (address < first->address)
    return NULL;

  ret = sysprof_kernel_symbol_lookup ((SysprofKernelSymbol *)(gpointer)kernel_symbols->data,
                                 address,
                                 0,
                                 kernel_symbols->len - 1);

  /* We resolve all symbols, including ignored symbols so that we
   * don't give back the wrong function juxtapose an ignored func.
   */
  if (ret != NULL && g_hash_table_contains (kernel_symbols_skip_hash, ret->name))
    return NULL;

  return ret;
}
