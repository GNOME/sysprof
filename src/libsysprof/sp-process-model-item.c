/* sp-process-model-item.c
 *
 * Copyright 2016 Christian Hergert <christian@hergert.me>
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

#include <string.h>

#include "sp-process-model-item.h"
#ifdef __linux__
# include "sp-proc-source.h"
#endif

struct _SpProcessModelItem
{
  GObject   parent_instance;
  GPid      pid;
  gchar    *command_line; /* Short version (first field) */
  gchar   **argv;         /* Long version (argv as a strv) */
  guint     is_kernel : 1;
};

G_DEFINE_TYPE (SpProcessModelItem, sp_process_model_item, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_COMMAND_LINE,
  PROP_PID,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

static void
sp_process_model_item_finalize (GObject *object)
{
  SpProcessModelItem *self = (SpProcessModelItem *)object;

  g_clear_pointer (&self->command_line, g_free);
  g_clear_pointer (&self->argv, g_strfreev);

  G_OBJECT_CLASS (sp_process_model_item_parent_class)->finalize (object);
}

static void
sp_process_model_item_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  SpProcessModelItem *self = SP_PROCESS_MODEL_ITEM (object);

  switch (prop_id)
    {
    case PROP_COMMAND_LINE:
      g_value_set_string (value, self->command_line);
      break;

    case PROP_PID:
      g_value_set_int (value, self->pid);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_process_model_item_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  SpProcessModelItem *self = SP_PROCESS_MODEL_ITEM (object);

  switch (prop_id)
    {
    case PROP_COMMAND_LINE:
      self->command_line = g_value_dup_string (value);
      break;

    case PROP_PID:
      self->pid = g_value_get_int (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_process_model_item_class_init (SpProcessModelItemClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sp_process_model_item_finalize;
  object_class->get_property = sp_process_model_item_get_property;
  object_class->set_property = sp_process_model_item_set_property;

  properties [PROP_COMMAND_LINE] =
    g_param_spec_string ("command-line",
                         "Command Line",
                         "Command Line",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_PID] =
    g_param_spec_int ("pid",
                      "Pid",
                      "Pid",
                      -1,
                      G_MAXINT,
                      -1,
                      (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sp_process_model_item_init (SpProcessModelItem *self)
{
}

SpProcessModelItem *
sp_process_model_item_new (GPid pid)
{
  g_autofree gchar *cmdline = NULL;
  SpProcessModelItem *ret;
  gboolean is_kernel = FALSE;

#ifdef __linux__
  cmdline = sp_proc_source_get_command_line (pid, &is_kernel);
#endif

  ret = g_object_new (SP_TYPE_PROCESS_MODEL_ITEM,
                      "command-line", cmdline,
                      "pid", (int)pid,
                      NULL);
  ret->is_kernel = is_kernel;

  return ret;
}

guint
sp_process_model_item_hash (SpProcessModelItem *self)
{
  g_return_val_if_fail (SP_IS_PROCESS_MODEL_ITEM (self), 0);

  return self->pid;
}

gboolean
sp_process_model_item_equal (SpProcessModelItem *self,
                             SpProcessModelItem *other)
{
  g_assert (SP_IS_PROCESS_MODEL_ITEM (self));
  g_assert (SP_IS_PROCESS_MODEL_ITEM (other));

  return ((self->pid == other->pid) &&
          (g_strcmp0 (self->command_line, other->command_line) == 0));
}

GPid
sp_process_model_item_get_pid (SpProcessModelItem *self)
{
  g_return_val_if_fail (SP_IS_PROCESS_MODEL_ITEM (self), 0);

  return self->pid;
}

const gchar *
sp_process_model_item_get_command_line (SpProcessModelItem *self)
{
  g_return_val_if_fail (SP_IS_PROCESS_MODEL_ITEM (self), NULL);

  return self->command_line;
}

gboolean
sp_process_model_item_is_kernel (SpProcessModelItem *self)
{
  g_return_val_if_fail (SP_IS_PROCESS_MODEL_ITEM (self), FALSE);

  return self->is_kernel;
}

const gchar * const *
sp_process_model_item_get_argv (SpProcessModelItem *self)
{
  g_autofree gchar *contents = NULL;
  g_autofree gchar *path = NULL;
  const gchar *pos;
  const gchar *endptr;
  GPtrArray *ar;
  gsize size = 0;
  GPid pid;

  g_return_val_if_fail (SP_IS_PROCESS_MODEL_ITEM (self), NULL);

  if (self->argv)
    return (const gchar * const *)self->argv;

  if ((pid = sp_process_model_item_get_pid (self)) < 0)
    return NULL;

  path = g_strdup_printf ("/proc/%u/cmdline", (guint)pid);
  if (!g_file_get_contents (path, &contents, &size, NULL))
    return NULL;

  ar = g_ptr_array_new ();

  /* Each parameter is followed by \0 */
  for (pos = contents, endptr = contents + size;
       pos < endptr;
       pos += strlen (pos) + 1)
    g_ptr_array_add (ar, g_strdup (pos));
  g_ptr_array_add (ar, NULL);

  g_clear_pointer (&self->argv, g_strfreev);
  self->argv = (gchar **)g_ptr_array_free (ar, FALSE);

  return (const gchar * const *)self->argv;
}

