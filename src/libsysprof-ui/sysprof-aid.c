/* sysprof-aid.c
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

#define G_LOG_DOMAIN "sysprof-aid"

#include "config.h"

#include <gtk/gtk.h>

#include "sysprof-aid.h"

typedef struct
{
  GPtrArray *sources;
  gchar     *display_name;
  GIcon     *icon;
} SysprofAidPrivate;

static void buildable_iface_init (GtkBuildableIface *iface);

G_DEFINE_TYPE_WITH_CODE (SysprofAid, sysprof_aid, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (SysprofAid)
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, buildable_iface_init))

enum {
  PROP_0,
  PROP_DISPLAY_NAME,
  PROP_ICON,
  PROP_ICON_NAME,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

static void
sysprof_aid_real_present_async (SysprofAid           *self,
                                SysprofCaptureReader *reader,
                                SysprofDisplay       *display,
                                GCancellable         *cancellable,
                                GAsyncReadyCallback   callback,
                                gpointer              user_data)
{
  g_task_report_new_error (self, callback, user_data,
                           sysprof_aid_real_present_async,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_SUPPORTED,
                           "Not supported");
}

static gboolean
sysprof_aid_real_present_finish (SysprofAid    *self,
                                 GAsyncResult  *result,
                                 GError       **error)
{
  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
sysprof_aid_finalize (GObject *object)
{
  SysprofAid *self = (SysprofAid *)object;
  SysprofAidPrivate *priv = sysprof_aid_get_instance_private (self);

  g_clear_pointer (&priv->sources, g_ptr_array_unref);
  g_clear_pointer (&priv->display_name, g_free);
  g_clear_object (&priv->icon);

  G_OBJECT_CLASS (sysprof_aid_parent_class)->finalize (object);
}

static void
sysprof_aid_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  SysprofAid *self = SYSPROF_AID (object);

  switch (prop_id)
    {
    case PROP_DISPLAY_NAME:
      g_value_set_string (value, sysprof_aid_get_display_name (self));
      break;

    case PROP_ICON:
      g_value_set_object (value, sysprof_aid_get_icon (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_aid_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  SysprofAid *self = SYSPROF_AID (object);

  switch (prop_id)
    {
    case PROP_DISPLAY_NAME:
      sysprof_aid_set_display_name (self, g_value_get_string (value));
      break;

    case PROP_ICON:
      sysprof_aid_set_icon (self, g_value_get_object (value));
      break;

    case PROP_ICON_NAME:
      sysprof_aid_set_icon_name (self, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_aid_class_init (SysprofAidClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_aid_finalize;
  object_class->get_property = sysprof_aid_get_property;
  object_class->set_property = sysprof_aid_set_property;

  klass->present_async = sysprof_aid_real_present_async;
  klass->present_finish = sysprof_aid_real_present_finish;

  properties [PROP_DISPLAY_NAME] =
    g_param_spec_string ("display-name",
                         "Display Name",
                         "Display Name",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_ICON_NAME] =
    g_param_spec_string ("icon-name",
                         "Icon Name",
                         "Icon Name",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_ICON] =
    g_param_spec_object ("icon",
                         "Icon",
                         "The icon to display",
                         G_TYPE_ICON,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_aid_init (SysprofAid *self G_GNUC_UNUSED)
{
}

/**
 * sysprof_aid_get_display_name:
 * @self: a #SysprofAid
 *
 * Gets the display name as it should be shown to the user.
 *
 * Returns: a string containing the display name
 *
 * Since: 3.34
 */
const gchar *
sysprof_aid_get_display_name (SysprofAid *self)
{
  SysprofAidPrivate *priv = sysprof_aid_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_AID (self), NULL);

  return priv->display_name;
}

/**
 * sysprof_aid_get_icon:
 *
 * Gets the icon for the aid.
 *
 * Returns: (transfer none) (nullable): a #GIcon or %NULL
 *
 * Since: 3.34
 */
GIcon *
sysprof_aid_get_icon (SysprofAid *self)
{
  SysprofAidPrivate *priv = sysprof_aid_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_AID (self), NULL);

  return priv->icon;
}

void
sysprof_aid_set_icon (SysprofAid *self,
                      GIcon      *icon)
{
  SysprofAidPrivate *priv = sysprof_aid_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_AID (self));

  if (g_set_object (&priv->icon, icon))
    g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_ICON]);
}

void
sysprof_aid_set_icon_name (SysprofAid  *self,
                           const gchar *icon_name)
{
  g_autoptr(GIcon) icon = NULL;

  g_return_if_fail (SYSPROF_IS_AID (self));

  if (icon_name != NULL)
    icon = g_themed_icon_new (icon_name);

  sysprof_aid_set_icon (self, icon);
}

void
sysprof_aid_set_display_name (SysprofAid  *self,
                              const gchar *display_name)
{
  SysprofAidPrivate *priv = sysprof_aid_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_AID (self));

  if (g_strcmp0 (display_name, priv->display_name) != 0)
    {
      g_free (priv->display_name);
      priv->display_name = g_strdup (display_name);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_DISPLAY_NAME]);
    }
}

void
sysprof_aid_prepare (SysprofAid      *self,
                     SysprofProfiler *profiler)
{
  SysprofAidPrivate *priv = sysprof_aid_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_AID (self));
  g_return_if_fail (SYSPROF_IS_PROFILER (profiler));

  if (priv->sources != NULL)
    {
      for (guint i = 0; i < priv->sources->len; i++)
        {
          SysprofSource *source = g_ptr_array_index (priv->sources, i);

          sysprof_profiler_add_source (profiler, source);
        }

      if (priv->sources->len > 0)
        g_ptr_array_remove_range (priv->sources, 0, priv->sources->len);
    }

  if (SYSPROF_AID_GET_CLASS (self)->prepare)
    SYSPROF_AID_GET_CLASS (self)->prepare (self, profiler);
}

static void
sysprof_aid_add_child (GtkBuildable       *buildable,
                       GtkBuilder         *builder,
                       GObject            *object,
                       const gchar        *type G_GNUC_UNUSED)
{
  SysprofAid *self = (SysprofAid *)buildable;
  SysprofAidPrivate *priv = sysprof_aid_get_instance_private (self);

  g_assert (SYSPROF_IS_AID (self));
  g_assert (GTK_IS_BUILDER (builder));
  g_assert (G_IS_OBJECT (object));

  if (SYSPROF_IS_SOURCE (object))
    {
      if (priv->sources == NULL)
        priv->sources = g_ptr_array_new_with_free_func (g_object_unref);
      g_ptr_array_add (priv->sources, g_object_ref (object));
      return;
    }

  g_warning ("Unsupported child type of %s: %s",
             G_OBJECT_TYPE_NAME (self),
             G_OBJECT_TYPE_NAME (object));
}

static void
buildable_iface_init (GtkBuildableIface *iface)
{
  iface->add_child = sysprof_aid_add_child;
}

SysprofAid *
sysprof_aid_new (const gchar *display_name,
                 const gchar *icon_name)
{
  return g_object_new (SYSPROF_TYPE_AID,
                       "display-aid", display_name,
                       "icon-name", icon_name,
                       NULL);
}

void
sysprof_aid_present_async (SysprofAid           *self,
                           SysprofCaptureReader *reader,
                           SysprofDisplay       *display,
                           GCancellable         *cancellable,
                           GAsyncReadyCallback   callback,
                           gpointer              user_data)
{
  g_return_if_fail (SYSPROF_IS_AID (self));
  g_return_if_fail (reader != NULL);
  g_return_if_fail (SYSPROF_IS_DISPLAY (display));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  SYSPROF_AID_GET_CLASS (self)->present_async (self, reader, display, cancellable, callback, user_data);
}

gboolean
sysprof_aid_present_finish (SysprofAid    *self,
                            GAsyncResult  *result,
                            GError       **error)
{
  g_return_val_if_fail (SYSPROF_IS_AID (self), FALSE);
  g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

  return SYSPROF_AID_GET_CLASS (self)->present_finish (self, result, error);
}
