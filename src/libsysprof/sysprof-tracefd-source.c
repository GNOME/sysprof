/* sysprof-tracefd-source.c
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

#define G_LOG_DOMAIN "sysprof-tracefd-source"

#include "config.h"

#include <errno.h>
#include <sysprof-capture.h>

#include "sysprof-capture-autocleanups.h"
#include "sysprof-platform.h"
#include "sysprof-tracefd-source.h"

typedef struct
{
  SysprofCaptureWriter *writer;
  gchar                *envvar;
  gint                  tracefd;
} SysprofTracefdSourcePrivate;

enum {
  PROP_0,
  PROP_ENVVAR,
  N_PROPS
};

static void source_iface_init (SysprofSourceInterface *iface);

G_DEFINE_TYPE_WITH_CODE (SysprofTracefdSource, sysprof_tracefd_source, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (SysprofTracefdSource)
                         G_IMPLEMENT_INTERFACE (SYSPROF_TYPE_SOURCE, source_iface_init))

static GParamSpec *properties [N_PROPS];

static void
sysprof_tracefd_source_finalize (GObject *object)
{
  SysprofTracefdSource *self = (SysprofTracefdSource *)object;
  SysprofTracefdSourcePrivate *priv = sysprof_tracefd_source_get_instance_private (self);

  g_clear_pointer (&priv->writer, sysprof_capture_writer_unref);
  g_clear_pointer (&priv->envvar, g_free);

  if (priv->tracefd != -1)
    {
      close (priv->tracefd);
      priv->tracefd = -1;
    }

  G_OBJECT_CLASS (sysprof_tracefd_source_parent_class)->finalize (object);
}

static void
sysprof_tracefd_source_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  SysprofTracefdSource *self = SYSPROF_TRACEFD_SOURCE (object);

  switch (prop_id)
    {
    case PROP_ENVVAR:
      g_value_set_string (value, sysprof_tracefd_source_get_envvar (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_tracefd_source_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  SysprofTracefdSource *self = SYSPROF_TRACEFD_SOURCE (object);

  switch (prop_id)
    {
    case PROP_ENVVAR:
      sysprof_tracefd_source_set_envvar (self, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_tracefd_source_class_init (SysprofTracefdSourceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_tracefd_source_finalize;
  object_class->get_property = sysprof_tracefd_source_get_property;
  object_class->set_property = sysprof_tracefd_source_set_property;

  properties [PROP_ENVVAR] =
    g_param_spec_string ("envvar",
                         "Environment Variable",
                         "The environment variable to set",
                         "SYSPROF_TRACE_FD",
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  
  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_tracefd_source_init (SysprofTracefdSource *self)
{
  SysprofTracefdSourcePrivate *priv = sysprof_tracefd_source_get_instance_private (self);

  priv->tracefd = -1;
  priv->envvar = g_strdup ("SYSPROF_TRACE_FD");
}

const gchar *
sysprof_tracefd_source_get_envvar (SysprofTracefdSource *self)
{
  SysprofTracefdSourcePrivate *priv = sysprof_tracefd_source_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_TRACEFD_SOURCE (self), NULL);

  return priv->envvar;
}

void
sysprof_tracefd_source_set_envvar (SysprofTracefdSource *self,
                                   const gchar          *envvar)
{
  SysprofTracefdSourcePrivate *priv = sysprof_tracefd_source_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_TRACEFD_SOURCE (self));

  if (envvar == NULL)
    envvar = "SYSPROF_TRACE_FD";

  if (g_strcmp0 (priv->envvar, envvar) != 0)
    {
      g_free (priv->envvar);
      priv->envvar = g_strdup (envvar);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_ENVVAR]);
    }
}

static void
sysprof_tracefd_source_modify_spawn (SysprofSource    *source,
                                     SysprofSpawnable *spawnable)
{
  SysprofTracefdSource *self = (SysprofTracefdSource *)source;
  SysprofTracefdSourcePrivate *priv = sysprof_tracefd_source_get_instance_private (self);
  g_autofree gchar *name = NULL;
  g_autofree gchar *fdstr = NULL;
  gint dest_fd;
  gint fd;

  g_assert (SYSPROF_IS_TRACEFD_SOURCE (self));
  g_assert (SYSPROF_IS_SPAWNABLE (spawnable));
  g_assert (priv->tracefd == -1);

  name = g_strdup_printf ("[sysprof-tracefd:%s]", priv->envvar);

  if (-1 == (fd = sysprof_memfd_create (name)))
    {
      g_warning ("Failed to create FD for tracefd capture: %s",
                 g_strerror (errno));
      return;
    }

  if (-1 == (priv->tracefd = dup (fd)))
    {
      g_warning ("Failed to duplicate tracefd for readback: %s",
                 g_strerror (errno));
      close (fd);
      return;
    }

  dest_fd = sysprof_spawnable_take_fd (spawnable, fd, -1);
  fdstr = g_strdup_printf ("%u", dest_fd);
  sysprof_spawnable_setenv (spawnable, priv->envvar, fdstr);
}

static void
sysprof_tracefd_source_serialize (SysprofSource *source,
                                  GKeyFile      *keyfile,
                                  const gchar   *group)
{
  SysprofTracefdSource *self = (SysprofTracefdSource *)source;
  SysprofTracefdSourcePrivate *priv = sysprof_tracefd_source_get_instance_private (self);

  g_assert (SYSPROF_IS_TRACEFD_SOURCE (self));
  g_assert (keyfile != NULL);
  g_assert (group != NULL);

  g_key_file_set_string (keyfile, group, "envvar", priv->envvar);
}

static void
sysprof_tracefd_source_deserialize (SysprofSource *source,
                                    GKeyFile      *keyfile,
                                    const gchar   *group)
{
  SysprofTracefdSource *self = (SysprofTracefdSource *)source;
  g_autofree gchar *envvar = NULL;

  g_assert (SYSPROF_IS_TRACEFD_SOURCE (self));
  g_assert (keyfile != NULL);
  g_assert (group != NULL);

  if ((envvar = g_key_file_get_string (keyfile, group, "envvar", NULL)))
    sysprof_tracefd_source_set_envvar (self, envvar);
}

static void
sysprof_tracefd_source_prepare (SysprofSource *source)
{
  g_assert (SYSPROF_IS_TRACEFD_SOURCE (source));

  sysprof_source_emit_ready (source);
}

static gboolean
sysprof_tracefd_source_get_is_ready (SysprofSource *source)
{
  g_assert (SYSPROF_IS_TRACEFD_SOURCE (source));

  return TRUE;
}

static void
sysprof_tracefd_source_stop (SysprofSource *source)
{
  SysprofTracefdSource *self = (SysprofTracefdSource *)source;
  SysprofTracefdSourcePrivate *priv = sysprof_tracefd_source_get_instance_private (self);

  g_assert (SYSPROF_IS_TRACEFD_SOURCE (self));

  if (priv->writer != NULL && priv->tracefd != -1)
    {
      g_autoptr(SysprofCaptureReader) reader = NULL;

      /* FIXME: Error handling is ignored here */
      if ((reader = sysprof_capture_reader_new_from_fd (priv->tracefd)))
        sysprof_capture_writer_cat (priv->writer, reader);

      priv->tracefd = -1;
    }

  sysprof_source_emit_finished (source);
}

static void
sysprof_tracefd_source_set_writer (SysprofSource        *source,
                                   SysprofCaptureWriter *writer)
{
  SysprofTracefdSource *self = (SysprofTracefdSource *)source;
  SysprofTracefdSourcePrivate *priv = sysprof_tracefd_source_get_instance_private (self);

  g_assert (SYSPROF_IS_TRACEFD_SOURCE (self));
  g_assert (writer != NULL);

  g_clear_pointer (&priv->writer, sysprof_capture_writer_unref);
  priv->writer = sysprof_capture_writer_ref (writer);
}

static void
source_iface_init (SysprofSourceInterface *iface)
{
  iface->deserialize = sysprof_tracefd_source_deserialize;
  iface->get_is_ready = sysprof_tracefd_source_get_is_ready;
  iface->modify_spawn = sysprof_tracefd_source_modify_spawn;
  iface->prepare = sysprof_tracefd_source_prepare;
  iface->serialize = sysprof_tracefd_source_serialize;
  iface->set_writer = sysprof_tracefd_source_set_writer;
  iface->stop = sysprof_tracefd_source_stop;
}

SysprofSource *
sysprof_tracefd_source_new (void)
{
  return g_object_new (SYSPROF_TYPE_TRACEFD_SOURCE, NULL);
}
