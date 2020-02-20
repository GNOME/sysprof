/* sysprof-battery-source.c
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

#define G_LOG_DOMAIN "sysprof-battery-source"

#include "config.h"

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "sysprof-backport-autocleanups.h"
#include "sysprof-battery-source.h"
#include "sysprof-helpers.h"

struct _SysprofBatterySource
{
  GObject               parent_instance;

  SysprofCaptureWriter *writer;
  GArray               *batteries;

  guint                 combined_id;
  guint                 poll_source;
};

typedef struct
{
  gchar id[32];
  gchar name[52];
  guint charge_full;
  guint charge_now;
  gint  charge_now_fd;
  guint counter_id;
} Battery;

static void source_iface_init (SysprofSourceInterface *);

G_DEFINE_TYPE_WITH_CODE (SysprofBatterySource, sysprof_battery_source, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (SYSPROF_TYPE_SOURCE, source_iface_init))

static void
battery_clear (gpointer data)
{
  Battery *b = data;

  if (b->charge_now_fd != -1)
    close (b->charge_now_fd);
}

static gboolean
sysprof_battery_source_get_is_ready (SysprofSource *source)
{
  return TRUE;
}

static void
sysprof_battery_source_prepare (SysprofSource *source)
{
  SysprofBatterySource *self = (SysprofBatterySource *)source;
  g_autoptr(GDir) dir = NULL;
  g_autoptr(GArray) counters = NULL;
  const gchar *name;

  g_assert (SYSPROF_IS_BATTERY_SOURCE (self));

#define BAT_BASE_PATH "/sys/class/power_supply/"

  counters = g_array_new (FALSE, FALSE, sizeof (SysprofCaptureCounter));

  if (!(dir = g_dir_open (BAT_BASE_PATH, 0, NULL)))
    goto emit_ready;

  while ((name = g_dir_read_name (dir)))
    {
      g_autofree gchar *type_path = g_strdup_printf (BAT_BASE_PATH "%s/type", name);
      g_autofree gchar *model_path = g_strdup_printf (BAT_BASE_PATH "%s/model_name", name);
      g_autofree gchar *charge_now_path = g_strdup_printf (BAT_BASE_PATH "%s/charge_now", name);
      g_autofree gchar *charge_full_path = g_strdup_printf (BAT_BASE_PATH "%s/charge_full", name);
      g_autofree gchar *type_data = NULL;
      g_autofree gchar *model_data = NULL;
      g_autofree gchar *charge_full_data = NULL;
      SysprofCaptureCounter ctr;
      Battery bat = {{0}};

      /* We dn't care about AC */
      if (g_strcmp0 (name, "AC") == 0)
        continue;

      if (!g_file_get_contents (type_path, &type_data, NULL, NULL) ||
          !g_str_has_prefix (type_data, "Battery"))
        continue;

      g_strlcpy (bat.id, name, sizeof bat.id);

      if (g_file_get_contents (model_path, &model_data, NULL, NULL))
        g_strlcpy (bat.name, model_data, sizeof bat.name);

      if (g_file_get_contents (charge_full_path, &charge_full_data, NULL, NULL))
        bat.charge_full = atoi (charge_full_data);

      /* Wait for first polling */
      bat.charge_now = 0;

      g_strstrip (bat.id);
      g_strstrip (bat.name);

      bat.charge_now_fd = open (charge_now_path, O_RDONLY);

      if (bat.charge_now_fd == -1)
        continue;

      bat.counter_id = sysprof_capture_writer_request_counter (self->writer, 1);

      g_strlcpy (ctr.category, "Battery Charge", sizeof ctr.category);
      g_strlcpy (ctr.name, bat.id, sizeof ctr.name);
      g_snprintf (ctr.description, sizeof ctr.description, "%s (µAh)", bat.name);
      ctr.id = bat.counter_id;
      ctr.type = SYSPROF_CAPTURE_COUNTER_INT64;

      g_array_append_val (self->batteries, bat);
      g_array_append_val (counters, ctr);
    }

  if (counters->len > 0)
    {
      SysprofCaptureCounter ctr = {{0}};

      self->combined_id = sysprof_capture_writer_request_counter (self->writer, 1);

      /* Add combined counter */
      g_strlcpy (ctr.category, "Battery Charge", sizeof ctr.category);
      g_strlcpy (ctr.name, "Combined", sizeof ctr.name);
      g_snprintf (ctr.description, sizeof ctr.description, "Combined Battery Charge (µAh)");
      ctr.id = self->combined_id;
      ctr.type = SYSPROF_CAPTURE_COUNTER_INT64;

      g_array_append_val (counters, ctr);

      sysprof_capture_writer_define_counters (self->writer,
                                              SYSPROF_CAPTURE_CURRENT_TIME,
                                              -1,
                                              -1,
                                              (gpointer)counters->data,
                                              counters->len);
    }

#undef BAT_BASE_PATH

emit_ready:
  sysprof_source_emit_ready (source);
}

static gboolean
battery_poll (Battery                    *battery,
              SysprofCaptureCounterValue *value)
{
  gint64 val;
  gssize len;
  gchar buf[32];

  g_assert (battery != NULL);

  if (battery->charge_now_fd == -1)
    return FALSE;

  if (lseek (battery->charge_now_fd, 0, SEEK_SET) != 0)
    {
      close (battery->charge_now_fd);
      battery->charge_now_fd = -1;
      return FALSE;
    }

  len = read (battery->charge_now_fd, buf, sizeof buf - 1);

  if (len < 0)
    {
      close (battery->charge_now_fd);
      battery->charge_now_fd = -1;
      return FALSE;
    }

  buf [len] = 0;

  val = atoi (buf);

  if (val != battery->charge_now)
    {
      battery->charge_now = val;
      value->v64 = val;
      return TRUE;
    }

  return FALSE;
}

static gboolean
sysprof_battery_source_poll_cb (gpointer data)
{
  SysprofBatterySource *self = data;
  g_autoptr(GArray) values = NULL;
  g_autoptr(GArray) ids = NULL;
  gint64 combined = 0;

  g_assert (SYSPROF_IS_BATTERY_SOURCE (self));

  values = g_array_new (FALSE, FALSE, sizeof (SysprofCaptureCounterValue));
  ids = g_array_new (FALSE, FALSE, sizeof (guint));

  for (guint i = 0; i < self->batteries->len; i++)
    {
      Battery *battery = &g_array_index (self->batteries, Battery, i);
      SysprofCaptureCounterValue value;

      if G_LIKELY (battery_poll (battery, &value))
        {
          combined += value.v64;
          g_array_append_val (ids, battery->counter_id);
          g_array_append_val (values, value);
        }
    }

  if (values->len > 0)
    {
      if (self->combined_id != 0)
        {
          SysprofCaptureCounterValue value = { .v64 = combined, };

          g_array_append_val (ids, self->combined_id);
          g_array_append_val (values, value);
        }

      sysprof_capture_writer_set_counters (self->writer,
                                           SYSPROF_CAPTURE_CURRENT_TIME,
                                           -1,
                                           -1,
                                           (gconstpointer)ids->data,
                                           (gconstpointer)values->data,
                                           ids->len);
    }

  return G_SOURCE_CONTINUE;
}

static void
sysprof_battery_source_start (SysprofSource *source)
{
  SysprofBatterySource *self = (SysprofBatterySource *)source;

  g_assert (SYSPROF_IS_BATTERY_SOURCE (self));

  self->poll_source = g_timeout_add_seconds (1, sysprof_battery_source_poll_cb, self);

  /* Poll immediately */
  sysprof_battery_source_poll_cb (self);
}

static void
sysprof_battery_source_stop (SysprofSource *source)
{
  SysprofBatterySource *self = (SysprofBatterySource *)source;

  g_assert (SYSPROF_IS_BATTERY_SOURCE (self));

  /* Poll one last time */
  sysprof_battery_source_poll_cb (self);

  g_clear_handle_id (&self->poll_source, g_source_remove);

  sysprof_source_emit_finished (source);
}

static void
sysprof_battery_source_set_writer (SysprofSource        *source,
                                   SysprofCaptureWriter *writer)
{
  SysprofBatterySource *self = (SysprofBatterySource *)source;

  g_assert (SYSPROF_IS_BATTERY_SOURCE (self));
  g_assert (writer != NULL);

  g_clear_pointer (&self->writer, sysprof_capture_writer_unref);
  self->writer = sysprof_capture_writer_ref (writer);
}

static void
source_iface_init (SysprofSourceInterface *iface)
{
  iface->get_is_ready = sysprof_battery_source_get_is_ready;
  iface->prepare = sysprof_battery_source_prepare;
  iface->set_writer = sysprof_battery_source_set_writer;
  iface->start = sysprof_battery_source_start;
  iface->stop = sysprof_battery_source_stop;
}

static void
sysprof_battery_source_finalize (GObject *object)
{
  SysprofBatterySource *self = (SysprofBatterySource *)object;

  g_clear_pointer (&self->batteries, g_array_unref);
  g_clear_pointer (&self->writer, sysprof_capture_writer_unref);

  G_OBJECT_CLASS (sysprof_battery_source_parent_class)->finalize (object);
}

static void
sysprof_battery_source_class_init (SysprofBatterySourceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_battery_source_finalize;
}

static void
sysprof_battery_source_init (SysprofBatterySource *self)
{
  self->batteries = g_array_new (FALSE, FALSE, sizeof (Battery));
  g_array_set_clear_func (self->batteries, battery_clear);
}

SysprofSource *
sysprof_battery_source_new (void)
{
  return g_object_new (SYSPROF_TYPE_BATTERY_SOURCE, NULL);
}
