/* sysprof-memory-pmu.c
 *
 * Copyright 2026 Christian Hergert <christian@sourceandstack.com>
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

#include "sysprof-memory-sampler-private.h"
#include "sysprof-util-private.h"

static char *
read_pmu_file (const char  *path,
               const char  *name,
               GError     **error)
{
  g_autofree char *filename = NULL;
  char *contents = NULL;

  g_assert (path != NULL);
  g_assert (name != NULL);

  filename = g_build_filename (path, name, NULL);

  if (!g_file_get_contents (filename, &contents, NULL, error))
    return NULL;

  return g_strstrip (contents);
}

static gboolean
apply_term (const char              *path,
            const char              *name,
            guint64                  value,
            struct perf_event_attr  *attr,
            GError                 **error)
{
  g_autofree char *filename = NULL;
  g_autofree char *format = NULL;
  g_auto(GStrv) parts = NULL;
  g_auto(GStrv) ranges = NULL;
  guint64 mask = 0;
  guint64 encoded = 0;
  guint bit = 0;

  g_assert (path != NULL);
  g_assert (name != NULL);
  g_assert (attr != NULL);

  if (strspn (name, "abcdefghijklmnopqrstuvwxyz0123456789_") != strlen (name))
    return FALSE;

  filename = g_build_filename ("format", name, NULL);

  if (!(format = read_pmu_file (path, filename, error)))
    return FALSE;

  parts = g_strsplit (format, ":", -1);

  if (g_strv_length (parts) != 2)
    return FALSE;

  ranges = g_strsplit (parts[1], ",", -1);

  for (guint i = 0; ranges[i] != NULL; i++)
    {
      g_auto(GStrv) limits = g_strsplit (ranges[i], "-", -1);
      guint64 first;
      guint64 last;

      if (g_strv_length (limits) > 2 ||
          !g_ascii_string_to_unsigned (limits[0], 10, 0, 63, &first, NULL))
        return FALSE;

      last = first;

      if (limits[1] && !g_ascii_string_to_unsigned (limits[1], 10, first, 63, &last, NULL))
        return FALSE;

      for (guint j = first; j <= last; j++)
        {
          if (bit >= 64 || (mask & (G_GUINT64_CONSTANT (1) << j)))
            return FALSE;

          mask |= G_GUINT64_CONSTANT (1) << j;
          encoded |= ((value >> bit++) & 1) << j;
        }
    }

  if (bit < 64 && (value >> bit) != 0)
    return FALSE;

  if (g_str_equal (parts[0], "config"))
    attr->config = (attr->config & ~mask) | encoded;
  else if (g_str_equal (parts[0], "config1"))
    attr->config1 = (attr->config1 & ~mask) | encoded;
  else if (g_str_equal (parts[0], "config2"))
    attr->config2 = (attr->config2 & ~mask) | encoded;
  else
    return FALSE;

  return TRUE;
}

gboolean
_sysprof_memory_pmu_configure (const char              *path,
                               guint                    latency,
                               struct perf_event_attr  *attr,
                               GArray                 **cpus,
                               GError                 **error)
{
  g_autofree char *type = NULL;
  g_autofree char *event = NULL;
  g_autofree char *cpu_list = NULL;
  g_autofree char *aux = NULL;
  g_auto(GStrv) terms = NULL;
  guint64 type_id;

  g_return_val_if_fail (path != NULL, FALSE);
  g_return_val_if_fail (attr != NULL, FALSE);
  g_return_val_if_fail (cpus != NULL, FALSE);

  *cpus = NULL;
  memset (attr, 0, sizeof *attr);

  if (!(type = read_pmu_file (path, "type", error)) ||
      !(event = read_pmu_file (path, "events/mem-loads", error)))
    return FALSE;

  aux = g_build_filename (path, "events", "mem-loads-aux", NULL);

  if (g_file_test (aux, G_FILE_TEST_EXISTS))
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_SUPPORTED,
                           "This PMU requires a mem-loads-aux group, which is not supported yet");

      return FALSE;
    }

  if (!g_ascii_string_to_unsigned (type, 10, 1, G_MAXUINT32, &type_id, error))
    return FALSE;

  attr->type = type_id;
  terms = g_strsplit (event, ",", -1);

  for (guint i = 0; terms[i] != NULL; i++)
    {
      g_auto(GStrv) pair = g_strsplit (terms[i], "=", -1);
      guint64 value;

      if (g_strv_length (pair) != 2 ||
          !g_ascii_string_to_unsigned (g_str_has_prefix (pair[1], "0x") ? pair[1] + 2 : pair[1],
                                       g_str_has_prefix (pair[1], "0x") ? 16 : 10,
                                       0, G_MAXUINT64, &value, NULL) ||
          !apply_term (path, pair[0], value, attr, error))
        goto invalid;
    }

  if (!apply_term (path, "ldlat", latency, attr, error))
    goto invalid;

  cpu_list = read_pmu_file (path, "cpus", NULL);

  if (cpu_list != NULL)
    *cpus = _sysprof_parse_cpu_list (cpu_list, error);
  else
    {
      g_autofree char *basename = g_path_get_basename (path);

      if (!g_str_equal (basename, "cpu"))
        goto invalid;

      *cpus = _sysprof_get_online_cpus (error);
    }

  return *cpus != NULL;

invalid:
  if (error != NULL && *error == NULL)
    g_set_error_literal (error,
                         G_IO_ERROR,
                         G_IO_ERROR_INVALID_DATA,
                         "Invalid or unsupported memory-event sysfs description");

  return FALSE;
}
