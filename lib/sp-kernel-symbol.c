/* sp-kernel-symbol.c
 *
 * Copyright (C) 2016 Christian Hergert <chergert@redhat.com>
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

#include "sp-line-reader.h"
#include "sp-kernel-symbol.h"

static GArray *kernel_symbols;
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

  NULL
};

static gint
sp_kernel_symbol_compare (gconstpointer a,
                          gconstpointer b)
{
  const SpKernelSymbol *syma = a;
  const SpKernelSymbol *symb = b;

  if (syma->address > symb->address)
    return 1;
  else if (syma->address == symb->address)
    return 0;
  else
    return -1;
}

static gboolean
sp_kernel_symbol_load (void)
{
  g_autofree gchar *contents = NULL;
  g_autoptr(GArray) ar = NULL;
  g_autoptr(GHashTable) skip = NULL;
  g_autoptr(SpLineReader) reader = NULL;
  const gchar *line;
  gsize len;
  guint i;

  skip = g_hash_table_new (g_str_hash, g_str_equal);
  for (i = 0; kernel_symbols_skip [i]; i++)
    g_hash_table_insert (skip, (gchar *)kernel_symbols_skip [i], NULL);

  ar = g_array_new (FALSE, TRUE, sizeof (SpKernelSymbol));

  if (!g_file_get_contents ("/proc/kallsyms", &contents, &len, NULL))
    {
      g_warning ("/proc/kallsyms is missing, kernel symbols will not be available");
      return FALSE;
    }

  reader = sp_line_reader_new (contents, len);

  while (NULL != (line = sp_line_reader_next (reader, &len)))
    {
      gchar **tokens;

      ((gchar *)line) [len] = '\0';

      tokens = g_strsplit_set (line, " \t", -1);

      if (tokens [0] && tokens [1] && tokens [2])
        {
          SpCaptureAddress address;
          gchar *endptr;

          if (g_hash_table_contains (skip, tokens [2]))
            {
              g_strfreev (tokens);
              continue;
            }

          address = g_ascii_strtoull (tokens [0], &endptr, 16);

          if (*endptr == '\0' &&
              (g_str_equal (tokens [1], "T") || g_str_equal (tokens [1], "t")))
            {
              SpKernelSymbol sym;

              sym.address = address;
              sym.name = g_steal_pointer (&tokens [2]);

              g_array_append_val (ar, sym);
            }
        }

      g_strfreev (tokens);
    }

  if (ar->len == 0)
    return FALSE;

  g_array_sort (ar, sp_kernel_symbol_compare);

  kernel_symbols = g_steal_pointer (&ar);

  return TRUE;
}

static const SpKernelSymbol *
sp_kernel_symbol_lookup (SpKernelSymbol   *symbols,
                         SpCaptureAddress  address,
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
        return sp_kernel_symbol_lookup (symbols, address, first, mid);
      else
        return sp_kernel_symbol_lookup (symbols, address, mid, last);
    }
}

/**
 * sp_kernel_symbol_from_address:
 * @address: the address of the instruction pointer
 *
 * Locates the kernel symbol that contains @address.
 *
 * Returns: (transfer none): An #SpKernelSymbol or %NULL.
 */
const SpKernelSymbol *
sp_kernel_symbol_from_address (SpCaptureAddress address)
{
  const SpKernelSymbol *first;

  if (G_UNLIKELY (kernel_symbols == NULL))
    {
      if (!sp_kernel_symbol_load ())
        return NULL;
    }

  g_assert (kernel_symbols != NULL);
  g_assert (kernel_symbols->len > 0);

  /* Short circuit if this is out of range */
  first = &g_array_index (kernel_symbols, SpKernelSymbol, 0);
  if (address < first->address)
    return NULL;

  return sp_kernel_symbol_lookup ((SpKernelSymbol *)(gpointer)kernel_symbols->data,
                                  address,
                                  0,
                                  kernel_symbols->len - 1);
}
