/* sysprof-powercap-usage.c
 *
 * Copyright 2026 Christian Hergert
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

#include <errno.h>
#include <stdlib.h>

#include "sysprof-powercap-usage.h"
#include "sysprof-instrument-private.h"
#include "sysprof-recording-private.h"
#include "sysprof-util-private.h"

#define POWERCAP_ROOT "/sys/class/powercap"
#define CPU_ROOT "/sys/devices/system/cpu"

typedef struct
{
  char  *path;
  guint  id;
} CounterFile;

typedef struct
{
  SysprofRecording *recording;
  DexFuture        *cancellable;
} Record;

typedef struct _SysprofPowercapUsage      SysprofPowercapUsage;
typedef struct _SysprofPowercapUsageClass SysprofPowercapUsageClass;

struct _SysprofPowercapUsage
{
  SysprofInstrument parent_instance;
};

struct _SysprofPowercapUsageClass
{
  SysprofInstrumentClass parent_class;
};

#define SYSPROF_TYPE_POWERCAP_USAGE (sysprof_powercap_usage_get_type ())
#define SYSPROF_IS_POWERCAP_USAGE(obj) G_TYPE_CHECK_INSTANCE_TYPE (obj, SYSPROF_TYPE_POWERCAP_USAGE)

G_DEFINE_FINAL_TYPE (SysprofPowercapUsage, sysprof_powercap_usage, SYSPROF_TYPE_INSTRUMENT)

static void
counter_file_free (gpointer data)
{
  CounterFile *file = data;

  g_free (file->path);
  g_free (file);
}

static void
record_free (gpointer data)
{
  Record *record = data;

  g_clear_object (&record->recording);
  dex_clear (&record->cancellable);
  g_free (record);
}

static void
add_counter (SysprofCaptureWriter *writer,
             GPtrArray            *files,
             const char           *path,
             const char           *category,
             const char           *name,
             const char           *description)
{
  g_autofree char *contents = NULL;
  SysprofCaptureCounter counter = { 0 };
  CounterFile *file;

  if (!(contents = dex_await_string (_sysprof_load_file (path), NULL)))
    return;

  file = g_new0 (CounterFile, 1);
  file->path = g_strdup (path);
  file->id = sysprof_capture_writer_request_counter (writer, 1);

  counter.id = file->id;
  counter.type = SYSPROF_CAPTURE_COUNTER_INT64;
  g_strlcpy (counter.category, category, sizeof counter.category);
  g_strlcpy (counter.name, name, sizeof counter.name);
  g_strlcpy (counter.description, description, sizeof counter.description);

  sysprof_capture_writer_define_counters (writer,
                                          SYSPROF_CAPTURE_CURRENT_TIME,
                                          -1,
                                          -1,
                                          &counter,
                                          1);

  g_ptr_array_add (files, file);
}

static void
powercap_discover (SysprofCaptureWriter *writer,
                   GPtrArray            *files,
                   GHashTable           *seen,
                   const char           *directory)
{
  g_autoptr(GPtrArray) children = NULL;

  if (!(children = dex_await_boxed (_sysprof_list_directory (directory), NULL)))
    return;

  for (guint i = 0; i < children->len; i++)
    {
      GFileInfo *info = g_ptr_array_index (children, i);
      const char *entry = g_file_info_get_name (info);
      const char *identity = g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_ID_FILE);
      g_autofree char *path = g_build_filename (directory, entry, NULL);
      g_autofree char *energy_path = NULL;
      g_autofree char *range_path = NULL;
      g_autofree char *name_path = NULL;
      g_autofree char *range = NULL;
      g_autofree char *name = NULL;
      g_autofree char *label = NULL;
      char *range_end = NULL;
      guint64 maximum;

      if (identity == NULL || g_file_info_get_file_type (info) != G_FILE_TYPE_DIRECTORY ||
          g_hash_table_contains (seen, identity))
        continue;

      g_hash_table_add (seen, g_strdup (identity));

      energy_path = g_build_filename (path, "energy_uj", NULL);
      range_path = g_build_filename (path, "max_energy_range_uj", NULL);
      name_path = g_build_filename (path, "name", NULL);

      if ((range = dex_await_string (_sysprof_load_file (range_path), NULL)))
        {
          errno = 0;
          maximum = g_ascii_strtoull (range, &range_end, 10);

          if (errno == 0 &&
              range_end != range &&
              maximum > 0 &&
              maximum <= G_MAXINT64)
            {
              name = dex_await_string (_sysprof_load_file (name_path), NULL);
              label = g_strdup_printf ("%s (%s)",
                                       name != NULL ? g_strstrip (name) : entry,
                                       entry);
              add_counter (writer,
                           files,
                           energy_path,
                           "Powercap Energy",
                           label,
                           g_strstrip (range));
            }
        }

      powercap_discover (writer, files, seen, path);
    }
}

static void
idle_discover (SysprofCaptureWriter *writer,
               GPtrArray            *files)
{
  g_autoptr(GPtrArray) cpus = NULL;

  if (!(cpus = dex_await_boxed (_sysprof_list_directory (CPU_ROOT), NULL)))
    return;

  for (guint i = 0; i < cpus->len; i++)
    {
      GFileInfo *cpu_info = g_ptr_array_index (cpus, i);
      const char *cpu = g_file_info_get_name (cpu_info);
      g_autofree char *directory = NULL;
      g_autoptr(GPtrArray) states = NULL;

      if (!g_str_has_prefix (cpu, "cpu") || !g_ascii_isdigit (cpu[3]))
        continue;

      directory = g_build_filename (CPU_ROOT, cpu, "cpuidle", NULL);

      if (!(states = dex_await_boxed (_sysprof_list_directory (directory), NULL)))
        continue;

      for (guint j = 0; j < states->len; j++)
        {
          GFileInfo *state_info = g_ptr_array_index (states, j);
          const char *state = g_file_info_get_name (state_info);
          g_autofree char *path = NULL;
          g_autofree char *name_path = NULL;
          g_autofree char *name = NULL;
          g_autofree char *label = NULL;

          if (!g_str_has_prefix (state, "state") || !g_ascii_isdigit (state[5]))
            continue;

          path = g_build_filename (directory, state, "time", NULL);
          name_path = g_build_filename (directory, state, "name", NULL);

          name = dex_await_string (_sysprof_load_file (name_path), NULL);
          label = g_strdup_printf ("%s %s",
                                   cpu,
                                   name != NULL ? g_strstrip (name) : state);
          add_counter (writer,
                       files,
                       path,
                       "CPUIdle Requested",
                       label,
                       "Kernel-requested idle time (µs cumulative)");

          if (files->len >= 2048)
            return;
        }
    }
}

static void
sample_counters (SysprofCaptureWriter *writer,
                 GPtrArray            *files)
{
  g_autoptr(GPtrArray) futures = g_ptr_array_new_with_free_func (dex_unref);
  g_autofree guint *ids = g_new (guint, files->len);
  g_autofree SysprofCaptureCounterValue *values = g_new0 (SysprofCaptureCounterValue, files->len);
  guint n_values = 0;

  for (guint i = 0; i < files->len; i++)
    {
      CounterFile *file = g_ptr_array_index (files, i);

      g_ptr_array_add (futures, _sysprof_load_file (file->path));
    }

  for (guint i = 0; i < files->len; i++)
    {
      CounterFile *file = g_ptr_array_index (files, i);
      g_autofree char *contents = NULL;
      char *end = NULL;
      guint64 value;

      if (!(contents = dex_await_string (dex_ref (g_ptr_array_index (futures, i)), NULL)))
        continue;

      errno = 0;
      value = g_ascii_strtoull (contents, &end, 10);

      if (errno != 0 || end == contents || value > G_MAXINT64)
        continue;

      ids[n_values] = file->id;
      values[n_values].v64 = value;
      n_values++;
    }

  if (n_values > 0)
    sysprof_capture_writer_set_counters (writer,
                                         SYSPROF_CAPTURE_CURRENT_TIME,
                                         -1,
                                         -1,
                                         ids,
                                         values,
                                         n_values);
}

static DexFuture *
sysprof_powercap_usage_record_fiber (gpointer user_data)
{
  Record *record = user_data;
  SysprofCaptureWriter *writer = _sysprof_recording_writer (record->recording);
  g_autoptr(GPtrArray) files = g_ptr_array_new_with_free_func (counter_file_free);
  g_autoptr(GHashTable) seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  powercap_discover (writer, files, seen, POWERCAP_ROOT);
  idle_discover (writer, files);

  while (dex_future_get_status (record->cancellable) == DEX_FUTURE_STATUS_PENDING)
    {
      g_autoptr(GError) error = NULL;

      sample_counters (writer, files);

      dex_await (dex_future_first (dex_ref (record->cancellable),
                                   dex_timeout_new_usec (G_USEC_PER_SEC), NULL),
                 &error);

      if (!g_error_matches (error, DEX_ERROR, DEX_ERROR_TIMED_OUT))
        break;
    }

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
sysprof_powercap_usage_record (SysprofInstrument *instrument,
                               SysprofRecording  *recording,
                               GCancellable      *cancellable)
{
  Record *record;

  g_assert (SYSPROF_IS_POWERCAP_USAGE (instrument));
  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (G_IS_CANCELLABLE (cancellable));

  record = g_new0 (Record, 1);
  record->recording = g_object_ref (recording);
  record->cancellable = dex_cancellable_new_from_cancellable (cancellable);

  return dex_scheduler_spawn (NULL,
                              0,
                              sysprof_powercap_usage_record_fiber,
                              record,
                              record_free);
}

static void
sysprof_powercap_usage_class_init (SysprofPowercapUsageClass *klass)
{
  SYSPROF_INSTRUMENT_CLASS (klass)->record = sysprof_powercap_usage_record;
}

static void
sysprof_powercap_usage_init (SysprofPowercapUsage *self)
{
}

/**
 * sysprof_powercap_usage_new:
 *
 * Creates an instrument for local powercap energy and CPU idle counters.
 *
 * Returns: (transfer full): a new instrument
 */
SysprofInstrument *
sysprof_powercap_usage_new (void)
{
  return g_object_new (SYSPROF_TYPE_POWERCAP_USAGE, NULL);
}
