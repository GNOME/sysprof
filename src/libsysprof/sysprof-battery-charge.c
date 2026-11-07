/* sysprof-battery-charge.c
 *
 * Copyright 2023 Christian Hergert <chergert@redhat.com>
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

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib-unix.h>
#include <glib/gstdio.h>

#include "line-reader-private.h"

#include "sysprof-battery-charge.h"
#include "sysprof-instrument-private.h"
#include "sysprof-recording-private.h"
#include "sysprof-util-private.h"

#define SYS_CLASS_POWER_SUPPLY "/sys/class/power_supply/"

struct _SysprofBatteryCharge
{
  SysprofInstrument parent_instance;
};

struct _SysprofBatteryChargeClass
{
  SysprofInstrumentClass parent_class;
};

G_DEFINE_FINAL_TYPE (SysprofBatteryCharge, sysprof_battery_charge, SYSPROF_TYPE_INSTRUMENT)

typedef struct _Record
{
  SysprofRecording *recording;
  DexFuture        *cancellable;
} Record;

static void
record_free (gpointer data)
{
  Record *record = data;

  g_clear_object (&record->recording);
  dex_clear (&record->cancellable);
  g_free (record);
}

static char **
collect_battery_names (void)
{
  g_autoptr(GPtrArray) names = g_ptr_array_new_with_free_func (g_free);
  g_autoptr(GPtrArray) children = NULL;

  if ((children = dex_await_boxed (_sysprof_list_directory (SYS_CLASS_POWER_SUPPLY), NULL)))
    {
      for (guint i = 0; i < children->len; i++)
        {
          GFileInfo *info = g_ptr_array_index (children, i);
          const char *name = g_file_info_get_name (info);

          if (strcmp (name, "AC") != 0)
            g_ptr_array_add (names, g_strdup (name));
        }
    }

  g_ptr_array_add (names, NULL);

  return (char **)g_ptr_array_free (g_steal_pointer (&names), FALSE);
}

typedef struct
{
  char  *path;
  guint  id;
  guint  status : 1;
} BatteryField;

static void
battery_field_free (gpointer data)
{
  BatteryField *field = data;

  g_free (field->path);
  g_free (field);
}

static void
add_battery_field (SysprofCaptureWriter *writer,
                   GPtrArray            *fields,
                   const char           *device,
                   const char           *filename,
                   const char           *category,
                   const char           *unit,
                   gboolean              status)
{
  g_autofree char *path = g_build_filename (SYS_CLASS_POWER_SUPPLY, device, filename, NULL);
  SysprofCaptureCounter counter = { 0 };
  g_autofree char *contents = NULL;
  BatteryField *field;

  if (!(contents = dex_await_string (_sysprof_load_file (path), NULL)))
    return;

  field = g_new0 (BatteryField, 1);
  field->path = g_steal_pointer (&path);
  field->id = sysprof_capture_writer_request_counter (writer, 1);
  field->status = status;

  counter.id = field->id;
  counter.type = SYSPROF_CAPTURE_COUNTER_INT64;
  g_strlcpy (counter.category, category, sizeof counter.category);
  g_strlcpy (counter.name, device, sizeof counter.name);
  g_snprintf (counter.description, sizeof counter.description, "%s (%s)", device, unit);

  sysprof_capture_writer_define_counters (writer,
                                          SYSPROF_CAPTURE_CURRENT_TIME,
                                          -1,
                                          -1,
                                          &counter,
                                          1);

  g_ptr_array_add (fields, field);
}

static DexFuture *
sysprof_battery_charge_record_fiber (gpointer user_data)
{
  g_autofree guint *ids = NULL;
  g_autofree SysprofCaptureCounterValue *values = NULL;
  g_autofree SysprofCaptureCounter *counters = NULL;
  SysprofCaptureWriter *writer;
  Record *record = user_data;
  g_autoptr(GPtrArray) charge_paths = NULL;
  g_autoptr(GPtrArray) fields = g_ptr_array_new_with_free_func (battery_field_free);
  g_auto(GStrv) names = NULL;
  guint n_names;
  guint n_counters = 1;

  g_assert (record != NULL);
  g_assert (SYSPROF_IS_RECORDING (record->recording));
  g_assert (DEX_IS_FUTURE (record->cancellable));

  writer = _sysprof_recording_writer (record->recording);
  names = collect_battery_names ();
  n_names = g_strv_length (names);

  /* Use some stack space for our counters and values. */
  ids = g_new0 (guint, n_names + 1);
  counters = g_new0 (SysprofCaptureCounter, n_names + 1);
  values = g_new0 (SysprofCaptureCounterValue, n_names + 1);

  /* Setup the combined counter which is the total charge of all of
   * the batteries we discover on the system.
   */
  counters[0].id = ids[0] = sysprof_capture_writer_request_counter (writer, 1);
  g_strlcpy (counters[0].category, "Battery Charge", sizeof counters[0].category);
  g_strlcpy (counters[0].name, "Combined", sizeof counters[0].name);
  g_snprintf (counters[0].description, sizeof counters[0].description, "Combined Battery Charge (µAh)");
  counters[0].type = SYSPROF_CAPTURE_COUNTER_INT64;
  counters[0].value.v64 = 0;

  charge_paths = g_ptr_array_new_with_free_func (g_free);

  for (guint i = 0; names[i]; i++)
    {
      SysprofCaptureCounter *counter = &counters[n_counters];
      const char *name = names[i];
      g_autofree char *charge_path = g_build_filename (SYS_CLASS_POWER_SUPPLY,
                                                       name,
                                                       "charge_now",
                                                       NULL);
      g_autofree char *model_name_path = g_build_filename (SYS_CLASS_POWER_SUPPLY,
                                                           name,
                                                           "model_name",
                                                           NULL);
      g_autofree char *type_path = g_build_filename (SYS_CLASS_POWER_SUPPLY, name, "type", NULL);
      g_autofree char *model_name_data = NULL;
      g_autofree char *type_data = NULL;
      g_autofree char *charge_data = NULL;

      if ((type_data = dex_await_string (_sysprof_load_file (type_path), NULL)))
        {
          if (g_str_has_prefix (type_data, "Battery"))
            {
              add_battery_field (writer, fields, name, "energy_now",
                                 "Battery Energy", "µWh", FALSE);
              add_battery_field (writer, fields, name, "power_now",
                                 "Battery Power", "µW snapshot", FALSE);
              add_battery_field (writer, fields, name, "capacity",
                                 "Battery Percent", "% snapshot", FALSE);
              add_battery_field (writer, fields, name, "status",
                                 "Battery Status", "1 discharging, 2 charging, 3 full", TRUE);
            }
          else
            add_battery_field (writer, fields, name, "online",
                               "External Power", "boolean", FALSE);
        }

      if (type_data == NULL || !g_str_has_prefix (type_data, "Battery") ||
          !(charge_data = dex_await_string (_sysprof_load_file (charge_path), NULL)))
        continue;

      counter->id = ids[n_counters] = sysprof_capture_writer_request_counter (writer, 1);
      counter->type = SYSPROF_CAPTURE_COUNTER_INT64;
      g_strlcpy (counter->category, "Battery Charge", sizeof counter->category);
      if ((model_name_data = dex_await_string (_sysprof_load_file (model_name_path), NULL)))
        g_strlcpy (counter->name, g_strstrip (model_name_data), sizeof counter->name);
      else
        g_strlcpy (counter->name, name, sizeof counter->name);
      g_snprintf (counter->description, sizeof counter->description, "%s (µAh)", counter->name);
      counter->value.v64 = 0;

      g_ptr_array_add (charge_paths, g_steal_pointer (&charge_path));

      n_counters++;
    }

  /* If we only have the combined counter, then just short-circuit and
   * don't record any counters. Otherwise the battery charge row might
   * show up in UI which we would want to omit.
   */
  if (n_counters == 1 && fields->len == 0)
    return dex_future_new_for_boolean (TRUE);

  if (n_counters > 1)
    sysprof_capture_writer_define_counters (writer,
                                            SYSPROF_CAPTURE_CURRENT_TIME,
                                            -1,
                                            -1,
                                            counters,
                                            n_counters);

  /* Main recording loop */
  for (;;)
    {
      g_autoptr(GPtrArray) futures = g_ptr_array_new_with_free_func (dex_unref);
      g_autoptr(GError) error = NULL;

      for (guint i = 0; i < charge_paths->len; i++)
        g_ptr_array_add (futures, _sysprof_load_file (g_ptr_array_index (charge_paths, i)));

      for (guint i = 0; i < fields->len; i++)
        {
          BatteryField *field = g_ptr_array_index (fields, i);

          g_ptr_array_add (futures, _sysprof_load_file (field->path));
        }

      /* Parse the current charge values */
      values[0].v64 = 0;
      for (guint i = 1; i <= charge_paths->len; i++)
        {
          g_autofree char *contents = NULL;
          guint64 v64;

          if (!(contents = dex_await_string (dex_ref (g_ptr_array_index (futures, i - 1)), NULL)) ||
              contents[0] == 0)
            continue;

          errno = 0;
          v64 = g_ascii_strtoull (contents, NULL, 10);
          if (v64 == G_MAXUINT64 || errno != 0)
            continue;

          values[i].v64 = v64;
          values[0].v64 += v64;
        }

      /* Deliver new values to capture */
      if (n_counters > 1)
        sysprof_capture_writer_set_counters (writer,
                                             SYSPROF_CAPTURE_CURRENT_TIME,
                                             -1,
                                             -1,
                                             ids,
                                             values,
                                             n_counters);

      for (guint i = 0; i < fields->len; i++)
        {
          BatteryField *field = g_ptr_array_index (fields, i);
          g_autofree char *contents = NULL;
          SysprofCaptureCounterValue value = { 0 };
          char *end = NULL;

          if (!(contents = dex_await_string (dex_ref (g_ptr_array_index (futures, charge_paths->len + i)), NULL)))
            continue;

          if (field->status)
            {
              if (g_str_has_prefix (contents, "Discharging"))
                value.v64 = 1;
              else if (g_str_has_prefix (contents, "Charging"))
                value.v64 = 2;
              else if (g_str_has_prefix (contents, "Full"))
                value.v64 = 3;
              else
                continue;
            }
          else
            {
              errno = 0;
              value.v64 = g_ascii_strtoull (contents, &end, 10);

              if (errno != 0 || end == contents || value.v64 > G_MAXINT64)
                continue;
            }

          sysprof_capture_writer_set_counters (writer,
                                               SYSPROF_CAPTURE_CURRENT_TIME,
                                               -1, -1, &field->id, &value, 1);
        }

      /* Wait for cancellation or ½ second */
      dex_await (dex_future_first (dex_ref (record->cancellable),
                                   dex_timeout_new_usec (G_USEC_PER_SEC / 2),
                                   NULL),
                 &error);

      if (!g_error_matches (error, DEX_ERROR, DEX_ERROR_TIMED_OUT))
        break;
    }

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
sysprof_battery_charge_record (SysprofInstrument *instrument,
                               SysprofRecording  *recording,
                               GCancellable      *cancellable)
{
  Record *record;

  g_assert (SYSPROF_IS_BATTERY_CHARGE (instrument));
  g_assert (SYSPROF_IS_RECORDING (recording));
  g_assert (G_IS_CANCELLABLE (cancellable));

  record = g_new0 (Record, 1);
  record->recording = g_object_ref (recording);
  record->cancellable = dex_cancellable_new_from_cancellable (cancellable);

  return dex_scheduler_spawn (NULL, 0,
                              sysprof_battery_charge_record_fiber,
                              record,
                              record_free);
}

static void
sysprof_battery_charge_class_init (SysprofBatteryChargeClass *klass)
{
  SysprofInstrumentClass *instrument_class = SYSPROF_INSTRUMENT_CLASS (klass);

  instrument_class->record = sysprof_battery_charge_record;
}

static void
sysprof_battery_charge_init (SysprofBatteryCharge *self)
{
}

SysprofInstrument *
sysprof_battery_charge_new (void)
{
  return g_object_new (SYSPROF_TYPE_BATTERY_CHARGE, NULL);
}
