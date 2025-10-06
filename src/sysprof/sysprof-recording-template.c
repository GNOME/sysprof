/* sysprof-recording-template.c
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

#include <json-glib/json-glib.h>

#include "sysprof-recording-template.h"
#include "sysprof-util.h"

#define DEFAULT_STACK_SIZE (4096*4)

struct _SysprofRecordingTemplate
{
  GObject parent_instance;

  char *command_line;
  char *cwd;
  char *power_profile;
  char **environ;
  char **debugdirs;

  guint stack_size;

  guint battery_charge : 1;
  guint bundle_symbols : 1;
  guint debuginfod : 1;
  guint clear_environ : 1;
  guint cpu_usage : 1;
  guint disk_usage : 1;
  guint energy_usage : 1;
  guint frame_timings : 1;
  guint graphics_info : 1;
  guint hardware_info : 1;
  guint javascript_stacks : 1;
  guint memory_allocations : 1;
  guint memory_usage : 1;
  guint native_stacks : 1;
  guint network_usage : 1;
  guint scheduler_details : 1;
  guint session_bus : 1;
  guint system_bus : 1;
  guint system_log : 1;
  guint user_stacks : 1;
};

enum {
  PROP_0,
  PROP_BATTERY_CHARGE,
  PROP_BUNDLE_SYMBOLS,
  PROP_DEBUGINFOD,
  PROP_DEBUGDIRS,
  PROP_CLEAR_ENVIRON,
  PROP_COMMAND_LINE,
  PROP_CPU_USAGE,
  PROP_CWD,
  PROP_DISK_USAGE,
  PROP_ENERGY_USAGE,
  PROP_ENVIRON,
  PROP_FRAME_TIMINGS,
  PROP_GRAPHICS_INFO,
  PROP_HARDWARE_INFO,
  PROP_JAVASCRIPT_STACKS,
  PROP_MEMORY_ALLOCATIONS,
  PROP_MEMORY_USAGE,
  PROP_NATIVE_STACKS,
  PROP_NETWORK_USAGE,
  PROP_POWER_PROFILE,
  PROP_SCHEDULER_DETAILS,
  PROP_SESSION_BUS,
  PROP_STACK_SIZE,
  PROP_SYSTEM_BUS,
  PROP_SYSTEM_LOG,
  PROP_USER_STACKS,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofRecordingTemplate, sysprof_recording_template, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static void
sysprof_recording_template_finalize (GObject *object)
{
  SysprofRecordingTemplate *self = (SysprofRecordingTemplate *)object;

  g_clear_pointer (&self->command_line, g_free);
  g_clear_pointer (&self->cwd, g_free);
  g_clear_pointer (&self->power_profile, g_free);
  g_clear_pointer (&self->environ, g_free);
  g_clear_pointer (&self->debugdirs, g_strfreev);

  G_OBJECT_CLASS (sysprof_recording_template_parent_class)->finalize (object);
}

static void
sysprof_recording_template_get_property (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
  SysprofRecordingTemplate *self = SYSPROF_RECORDING_TEMPLATE (object);

  switch (prop_id)
    {
    case PROP_BATTERY_CHARGE:
      g_value_set_boolean (value, self->battery_charge);
      break;

    case PROP_BUNDLE_SYMBOLS:
      g_value_set_boolean (value, self->bundle_symbols);
      break;
    
    case PROP_DEBUGINFOD:
      g_value_set_boolean (value, self->debuginfod);
      break;

    case PROP_CLEAR_ENVIRON:
      g_value_set_boolean (value, self->clear_environ);
      break;

    case PROP_COMMAND_LINE:
      g_value_set_string (value, self->command_line);
      break;

    case PROP_CPU_USAGE:
      g_value_set_boolean (value, self->cpu_usage);
      break;

    case PROP_CWD:
      g_value_set_string (value, self->cwd);
      break;

    case PROP_DISK_USAGE:
      g_value_set_boolean (value, self->disk_usage);
      break;

    case PROP_ENERGY_USAGE:
      g_value_set_boolean (value, self->energy_usage);
      break;

    case PROP_ENVIRON:
      g_value_set_boxed (value, self->environ);
      break;
    
    case PROP_DEBUGDIRS:
      g_value_set_boxed (value, self->debugdirs);
      break;

    case PROP_FRAME_TIMINGS:
      g_value_set_boolean (value, self->frame_timings);
      break;

    case PROP_GRAPHICS_INFO:
      g_value_set_boolean (value, self->graphics_info);
      break;

    case PROP_HARDWARE_INFO:
      g_value_set_boolean (value, self->hardware_info);
      break;

    case PROP_JAVASCRIPT_STACKS:
      g_value_set_boolean (value, self->javascript_stacks);
      break;

    case PROP_MEMORY_ALLOCATIONS:
      g_value_set_boolean (value, self->memory_allocations);
      break;

    case PROP_MEMORY_USAGE:
      g_value_set_boolean (value, self->memory_usage);
      break;

    case PROP_NATIVE_STACKS:
      g_value_set_boolean (value, self->native_stacks);
      break;

    case PROP_NETWORK_USAGE:
      g_value_set_boolean (value, self->network_usage);
      break;

    case PROP_POWER_PROFILE:
      g_value_set_string (value, self->power_profile);
      break;

    case PROP_SCHEDULER_DETAILS:
      g_value_set_boolean (value, self->scheduler_details);
      break;

    case PROP_SESSION_BUS:
      g_value_set_boolean (value, self->session_bus);
      break;

    case PROP_STACK_SIZE:
      g_value_set_uint (value, self->stack_size);
      break;

    case PROP_SYSTEM_BUS:
      g_value_set_boolean (value, self->system_bus);
      break;

    case PROP_SYSTEM_LOG:
      g_value_set_boolean (value, self->system_log);
      break;

    case PROP_USER_STACKS:
      g_value_set_boolean (value, self->user_stacks);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_recording_template_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
  SysprofRecordingTemplate *self = SYSPROF_RECORDING_TEMPLATE (object);

  switch (prop_id)
    {
    case PROP_BATTERY_CHARGE:
      self->battery_charge = g_value_get_boolean (value);
      break;

    case PROP_BUNDLE_SYMBOLS:
      self->bundle_symbols = g_value_get_boolean (value);
      break;
    
    case PROP_DEBUGINFOD:
      self->debuginfod = g_value_get_boolean (value);
      break;

    case PROP_CLEAR_ENVIRON:
      self->clear_environ = g_value_get_boolean (value);
      break;

    case PROP_COMMAND_LINE:
      g_set_str (&self->command_line, g_value_get_string (value));
      break;

    case PROP_CPU_USAGE:
      self->cpu_usage = g_value_get_boolean (value);
      break;

    case PROP_CWD:
      g_set_str (&self->cwd, g_value_get_string (value));
      break;

    case PROP_DISK_USAGE:
      self->disk_usage = g_value_get_boolean (value);
      break;

    case PROP_ENERGY_USAGE:
      self->energy_usage = g_value_get_boolean (value);
      break;

    case PROP_ENVIRON:
      g_clear_pointer (&self->environ, g_strfreev);
      self->environ = g_value_dup_boxed (value);
      break;
    
    case PROP_DEBUGDIRS:
      g_clear_pointer (&self->debugdirs, g_strfreev);
      self->debugdirs = g_value_dup_boxed (value);
      break;

    case PROP_FRAME_TIMINGS:
      self->frame_timings = g_value_get_boolean (value);
      break;

    case PROP_GRAPHICS_INFO:
      self->graphics_info = g_value_get_boolean (value);
      break;

    case PROP_HARDWARE_INFO:
      self->hardware_info = g_value_get_boolean (value);
      break;

    case PROP_JAVASCRIPT_STACKS:
      self->javascript_stacks = g_value_get_boolean (value);
      break;

    case PROP_MEMORY_ALLOCATIONS:
      self->memory_allocations = g_value_get_boolean (value);
      break;

    case PROP_MEMORY_USAGE:
      self->memory_usage = g_value_get_boolean (value);
      break;

    case PROP_NATIVE_STACKS:
      self->native_stacks = g_value_get_boolean (value);
      break;

    case PROP_NETWORK_USAGE:
      self->network_usage = g_value_get_boolean (value);
      break;

    case PROP_POWER_PROFILE:
      g_set_str (&self->power_profile, g_value_get_string (value));
      break;

    case PROP_SCHEDULER_DETAILS:
      self->scheduler_details = g_value_get_boolean (value);
      break;

    case PROP_SESSION_BUS:
      self->session_bus = g_value_get_boolean (value);
      break;

    case PROP_STACK_SIZE:
      self->stack_size = g_value_get_uint (value);
      break;

    case PROP_SYSTEM_BUS:
      self->system_bus = g_value_get_boolean (value);
      break;

    case PROP_SYSTEM_LOG:
      self->system_log = g_value_get_boolean (value);
      break;

    case PROP_USER_STACKS:
      self->user_stacks = g_value_get_boolean (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_recording_template_class_init (SysprofRecordingTemplateClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_recording_template_finalize;
  object_class->get_property = sysprof_recording_template_get_property;
  object_class->set_property = sysprof_recording_template_set_property;

  properties[PROP_BATTERY_CHARGE] =
    g_param_spec_boolean ("battery-charge", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_BUNDLE_SYMBOLS] =
    g_param_spec_boolean ("bundle-symbols", NULL, NULL,
                          TRUE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
                      
  properties[PROP_DEBUGINFOD] =
    g_param_spec_boolean ("debuginfod", NULL, NULL,
                          TRUE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_CLEAR_ENVIRON] =
    g_param_spec_boolean ("clear-environ", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_COMMAND_LINE] =
    g_param_spec_string ("command-line", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_CPU_USAGE] =
    g_param_spec_boolean ("cpu-usage", NULL, NULL,
                          TRUE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_CWD] =
    g_param_spec_string ("cwd", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_DISK_USAGE] =
    g_param_spec_boolean ("disk-usage", NULL, NULL,
                          TRUE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_ENERGY_USAGE] =
    g_param_spec_boolean ("energy-usage", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_ENVIRON] =
    g_param_spec_boxed ("environ", NULL, NULL,
                        G_TYPE_STRV,
                        (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  
  properties[PROP_DEBUGDIRS] =
    g_param_spec_boxed ("debugdirs", NULL, NULL,
                        G_TYPE_STRV,
                        (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_FRAME_TIMINGS] =
    g_param_spec_boolean ("frame-timings", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_GRAPHICS_INFO] =
    g_param_spec_boolean ("graphics-info", NULL, NULL,
                          TRUE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_HARDWARE_INFO] =
    g_param_spec_boolean ("hardware-info", NULL, NULL,
                          TRUE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_JAVASCRIPT_STACKS] =
    g_param_spec_boolean ("javascript-stacks", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_MEMORY_ALLOCATIONS] =
    g_param_spec_boolean ("memory-allocations", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_MEMORY_USAGE] =
    g_param_spec_boolean ("memory-usage", NULL, NULL,
                          TRUE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_NATIVE_STACKS] =
    g_param_spec_boolean ("native-stacks", NULL, NULL,
                          TRUE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_NETWORK_USAGE] =
    g_param_spec_boolean ("network-usage", NULL, NULL,
                          TRUE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_POWER_PROFILE] =
    g_param_spec_string ("power-profile", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_SCHEDULER_DETAILS] =
    g_param_spec_boolean ("scheduler-details", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_SESSION_BUS] =
    g_param_spec_boolean ("session-bus", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_SYSTEM_BUS] =
    g_param_spec_boolean ("system-bus", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_SYSTEM_LOG] =
    g_param_spec_boolean ("system-log", NULL, NULL,
                          TRUE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_USER_STACKS] =
    g_param_spec_boolean ("user-stacks", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties[PROP_STACK_SIZE] =
    g_param_spec_uint ("stack-size", NULL, NULL,
                       0, G_MAXUINT, DEFAULT_STACK_SIZE,
                       (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_recording_template_init (SysprofRecordingTemplate *self)
{
  self->bundle_symbols = TRUE;
  self->debuginfod = TRUE;
  self->cpu_usage = TRUE;
  self->disk_usage = TRUE;
  self->graphics_info = TRUE;
  self->hardware_info = TRUE;
  self->memory_usage = TRUE;
  self->native_stacks = TRUE;
  self->network_usage = TRUE;
  self->scheduler_details = FALSE;
  self->system_log = TRUE;
  self->command_line = g_strdup ("");
  self->cwd = g_strdup("");
  self->environ = g_strdupv((char * []) { NULL });
  self->debugdirs = g_strdupv((char * []) { NULL });
  self->stack_size = DEFAULT_STACK_SIZE;
}

SysprofRecordingTemplate *
sysprof_recording_template_new (void)
{
  return g_object_new (SYSPROF_TYPE_RECORDING_TEMPLATE, NULL);
}

static gboolean
environ_parse (const char  *pair,
               char       **key,
               char       **value)
{
  const gchar *eq;

  g_return_val_if_fail (pair != NULL, FALSE);

  if (key != NULL)
    *key = NULL;

  if (value != NULL)
    *value = NULL;

  if ((eq = strchr (pair, '=')))
    {
      if (key != NULL)
        *key = g_strndup (pair, eq - pair);

      if (value != NULL)
        *value = g_strdup (eq + 1);

      return TRUE;
    }

  return FALSE;
}

static void
add_trace_fd (SysprofProfiler  *profiler,
              SysprofSpawnable *spawnable,
              const char       *name)
{
  int trace_fd;

  g_assert (SYSPROF_IS_PROFILER (profiler));
  g_assert (!spawnable || SYSPROF_IS_SPAWNABLE (spawnable));

  if (spawnable == NULL)
    return;

  trace_fd = sysprof_spawnable_add_trace_fd (spawnable, name);
  sysprof_profiler_add_instrument (profiler, sysprof_tracefd_consumer_new (trace_fd));
}

SysprofProfiler *
sysprof_recording_template_apply (SysprofRecordingTemplate  *self,
                                  GError                   **error)
{
  g_autoptr(SysprofProfiler) profiler = NULL;

  g_return_val_if_fail (SYSPROF_IS_RECORDING_TEMPLATE (self), NULL);

  profiler = sysprof_profiler_new ();

  if (self->command_line && self->command_line[0])
    {
      g_autofree char *stripped = g_strstrip (g_strdup (self->command_line));
      g_autoptr(SysprofSpawnable) spawnable = NULL;
      g_autoptr(GError) local_error = NULL;
      g_auto(GStrv) argv = NULL;
      g_auto(GStrv) env = NULL;
      int argc;

      if (!g_shell_parse_argv (stripped, &argc, &argv, &local_error))
        {
          g_set_error_literal (error,
                               SYSPROF_RECORDING_TEMPLATE_ERROR,
                               SYSPROF_RECORDING_TEMPLATE_ERROR_COMMAND_LINE,
                               local_error->message);
          return FALSE;
        }

      spawnable = sysprof_spawnable_new ();

      if (g_file_test ("/.flatpak-info", G_FILE_TEST_EXISTS) &&
          !g_strv_contains ((const char * const *)argv, "flatpak-spawn"))
        {
          sysprof_spawnable_append_argv (spawnable, "flatpak-spawn");
          sysprof_spawnable_append_argv (spawnable, "--host");
          sysprof_spawnable_append_argv (spawnable, "--watch-bus");
        }

      sysprof_spawnable_append_args (spawnable, (const char * const *)argv);

      if (self->cwd && self->cwd[0])
        sysprof_spawnable_set_cwd (spawnable, self->cwd);

      if (!self->clear_environ)
        env = g_get_environ ();

      if (self->environ)
        {
          for (guint i = 0; self->environ[i]; i++)
            {
              g_autofree char *key = NULL;
              g_autofree char *value = NULL;

              if (environ_parse (self->environ[i], &key, &value))
                env = g_environ_setenv (env, key, value, TRUE);
            }
        }

      if (self->memory_allocations)
        sysprof_spawnable_add_ld_preload (spawnable, PACKAGE_LIBDIR"/libsysprof-memory-"API_VERSION_S".so");

      sysprof_profiler_set_spawnable (profiler, spawnable);

      if (self->javascript_stacks)
        {
          sysprof_spawnable_setenv (spawnable, "GJS_ENABLE_PROFILER", "1");
          add_trace_fd (profiler, spawnable, "GJS_TRACE_FD");
        }
    }

  if (self->power_profile && self->power_profile[0])
    sysprof_profiler_add_instrument (profiler, sysprof_power_profile_new (self->power_profile));

  if (self->battery_charge)
    sysprof_profiler_add_instrument (profiler, sysprof_battery_charge_new ());

  if (self->bundle_symbols)
    sysprof_profiler_add_instrument (profiler, sysprof_symbols_bundle_new ());
  
  if (self->cpu_usage)
    sysprof_profiler_add_instrument (profiler, sysprof_cpu_usage_new ());

  if (self->disk_usage)
    sysprof_profiler_add_instrument (profiler, sysprof_disk_usage_new ());

  if (self->energy_usage)
    sysprof_profiler_add_instrument (profiler, sysprof_energy_usage_new ());

  if (self->frame_timings)
    sysprof_profiler_add_instrument (profiler,
                                     sysprof_proxied_instrument_new (G_BUS_TYPE_SESSION,
                                                                     "org.gnome.Shell",
                                                                     "/org/gnome/Sysprof3/Profiler"));

  if (self->graphics_info)
    {
      sysprof_profiler_add_instrument (profiler,
                                       g_object_new (SYSPROF_TYPE_SUBPROCESS_OUTPUT,
                                                     "stdout-path", "eglinfo",
                                                     "command-argv", (const char * const[]) {"eglinfo", NULL},
                                                     NULL));
      sysprof_profiler_add_instrument (profiler,
                                       g_object_new (SYSPROF_TYPE_SUBPROCESS_OUTPUT,
                                                     "stdout-path", "glxinfo",
                                                     "command-argv", (const char * const[]) {"glxinfo", NULL},
                                                     NULL));
    }

  if (self->hardware_info)
    {
      sysprof_profiler_add_instrument (profiler,
                                       g_object_new (SYSPROF_TYPE_SUBPROCESS_OUTPUT,
                                                     "stdout-path", "lsusb",
                                                     "command-argv", (const char * const[]) {"lsusb", "-v", NULL},
                                                     NULL));
      sysprof_profiler_add_instrument (profiler,
                                       g_object_new (SYSPROF_TYPE_SUBPROCESS_OUTPUT,
                                                     "stdout-path", "lspci",
                                                     "command-argv", (const char * const[]) {"lspci", "-v", NULL},
                                                     NULL));
    }

  if (self->memory_usage)
    sysprof_profiler_add_instrument (profiler, sysprof_memory_usage_new ());

  if (self->native_stacks)
    {
      if (self->user_stacks)
        sysprof_profiler_add_instrument (profiler, sysprof_user_sampler_new (self->stack_size));
      else
        sysprof_profiler_add_instrument (profiler, sysprof_sampler_new ());
    }

  if (self->network_usage)
    sysprof_profiler_add_instrument (profiler, sysprof_network_usage_new ());

  if (self->scheduler_details)
    sysprof_profiler_add_instrument (profiler, sysprof_scheduler_details_new ());

  if (self->session_bus)
    sysprof_profiler_add_instrument (profiler, sysprof_dbus_monitor_new (G_BUS_TYPE_SESSION));

  if (self->system_bus)
    sysprof_profiler_add_instrument (profiler, sysprof_dbus_monitor_new (G_BUS_TYPE_SYSTEM));

  if (self->system_log)
    sysprof_profiler_add_instrument (profiler, sysprof_system_logs_new ());

  return g_steal_pointer (&profiler);
}

G_DEFINE_QUARK (SysprofRecordingTemplateError, sysprof_recording_template_error)

SysprofRecordingTemplate *
sysprof_recording_template_new_from_file (GFile   *file,
                                          GError **error)
{
  g_autoptr(JsonParser) parser = NULL;
  g_autoptr(GFile) state_file = NULL;
  SysprofRecordingTemplate *self;
  JsonNode *root;

  g_return_val_if_fail (!file || G_IS_FILE (file), NULL);

  if (file == NULL)
    file = state_file = _get_default_state_file ();

  parser = json_parser_new ();

  if (g_file_is_native (file))
    {
      g_autofree char *path = g_file_get_path (file);

      if (!json_parser_load_from_file (parser, path, error))
        return NULL;
    }
  else
    {
      g_autoptr(GFileInputStream) stream = g_file_read (file, NULL, error);

      if (stream == NULL)
        return NULL;

      if (!json_parser_load_from_stream (parser, G_INPUT_STREAM (stream), NULL, error))
        return NULL;
    }

  root = json_parser_get_root (parser);

  if (!(self = SYSPROF_RECORDING_TEMPLATE (json_gobject_deserialize (SYSPROF_TYPE_RECORDING_TEMPLATE, root))))
    return NULL;

  return g_steal_pointer (&self);
}

gboolean
sysprof_recording_template_save (SysprofRecordingTemplate  *self,
                                 GFile                     *file,
                                 GError                   **error)
{
  g_autoptr(GFileOutputStream) stream = NULL;
  g_autoptr(JsonGenerator) generator = NULL;
  g_autoptr(JsonNode) root = NULL;

  g_return_val_if_fail (SYSPROF_IS_RECORDING_TEMPLATE (self), FALSE);
  g_return_val_if_fail (G_IS_FILE (file), FALSE);

  if (!(stream = g_file_replace (file, NULL, FALSE, G_FILE_CREATE_REPLACE_DESTINATION, NULL, error)))
    return FALSE;

  root = json_gobject_serialize (G_OBJECT (self));
  generator = json_generator_new ();

  json_generator_set_root (generator, root);
  json_generator_set_pretty (generator, TRUE);

  return json_generator_to_stream (generator, G_OUTPUT_STREAM (stream), NULL, error);
}

static void
sysprof_recording_template_setup_loader (SysprofRecordingTemplate *self,
                                         SysprofDocumentLoader    *loader)
{
  g_autoptr(SysprofMultiSymbolizer) multi = NULL;
  g_autoptr(SysprofElfSymbolizer) elf = NULL;

  g_assert (SYSPROF_IS_RECORDING_TEMPLATE (self));
  g_assert (SYSPROF_IS_DOCUMENT_LOADER (loader));

  multi = sysprof_multi_symbolizer_new ();

  elf = SYSPROF_ELF_SYMBOLIZER (sysprof_elf_symbolizer_new ());

  if (self->debugdirs)
    sysprof_elf_symbolizer_set_external_debug_dirs (elf, (const char * const *)self->debugdirs);

  /* Add in order of priority */
  sysprof_multi_symbolizer_take (multi, sysprof_bundled_symbolizer_new ());
  sysprof_multi_symbolizer_take (multi, sysprof_kallsyms_symbolizer_new ());
  sysprof_multi_symbolizer_take (multi, SYSPROF_SYMBOLIZER (g_steal_pointer (&elf)));
  sysprof_multi_symbolizer_take (multi, sysprof_jitmap_symbolizer_new ());

  sysprof_document_loader_set_symbolizer (loader, SYSPROF_SYMBOLIZER (multi));

#if HAVE_DEBUGINFOD
  if (self->debuginfod)
      {
        g_autoptr(SysprofSymbolizer) debuginfod = NULL;
        g_autoptr(GError) debuginfod_error = NULL;

        if (!(debuginfod = sysprof_debuginfod_symbolizer_new (&debuginfod_error)))
          g_warning ("Failed to create debuginfod symbolizer: %s", debuginfod_error->message);
        else
          sysprof_multi_symbolizer_take (multi, g_steal_pointer (&debuginfod));
      }
#endif
}

SysprofDocumentLoader *
sysprof_recording_template_create_loader (SysprofRecordingTemplate  *self,
                                          GFile                     *file,
                                          GError                   **error)
{
  g_autoptr(SysprofDocumentLoader) loader = NULL;

  g_return_val_if_fail (SYSPROF_IS_RECORDING_TEMPLATE (self), NULL);
  g_return_val_if_fail (G_IS_FILE (file), NULL);

  if (!(loader = sysprof_document_loader_new (g_file_peek_path (file))))
    return NULL;

  sysprof_recording_template_setup_loader (self, loader);

  return g_steal_pointer (&loader);
}

SysprofDocumentLoader *
sysprof_recording_template_create_loader_for_fd (SysprofRecordingTemplate  *self,
                                                 int                        fd,
                                                 GError                   **error)
{
  g_autoptr(SysprofDocumentLoader) loader = NULL;

  g_return_val_if_fail (SYSPROF_IS_RECORDING_TEMPLATE (self), NULL);

  if (!(loader = sysprof_document_loader_new_for_fd (fd, error)))
    return NULL;

  sysprof_recording_template_setup_loader (self, loader);

  return g_steal_pointer (&loader);
}
