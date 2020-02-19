/* sysprof-page.c
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

#define G_LOG_DOMAIN "sysprof-page"

#include "config.h"

#include "sysprof-display.h"
#include "sysprof-page.h"
#include "sysprof-ui-private.h"

typedef struct
{
  gchar *title;
} SysprofPagePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (SysprofPage, sysprof_page, GTK_TYPE_BIN)

enum {
  PROP_0,
  PROP_TITLE,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

/**
 * sysprof_page_new:
 *
 * Create a new #SysprofPage.
 *
 * Returns: (transfer full) (type SysprofPage): a newly created #SysprofPage
 */
GtkWidget *
sysprof_page_new (void)
{
  return g_object_new (SYSPROF_TYPE_PAGE, NULL);
}

const gchar *
sysprof_page_get_title (SysprofPage *self)
{
  SysprofPagePrivate *priv = sysprof_page_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_PAGE (self), NULL);

  return priv->title;
}

void
sysprof_page_set_title (SysprofPage *self,
                        const gchar *title)
{
  SysprofPagePrivate *priv = sysprof_page_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_PAGE (self));

  if (g_strcmp0 (priv->title, title) != 0)
    {
      g_free (priv->title);
      priv->title = g_strdup (title);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_TITLE]);
    }
}

static void
sysprof_page_real_load_async (SysprofPage             *self,
                              SysprofCaptureReader    *reader,
                              SysprofSelection        *selection,
                              SysprofCaptureCondition *condition,
                              GCancellable            *cancellable,
                              GAsyncReadyCallback      callback,
                              gpointer                 user_data)
{
  g_task_report_new_error (self, callback, user_data,
                           sysprof_page_load_async,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_SUPPORTED,
                           "Operation not supported");
}

static gboolean
sysprof_page_real_load_finish (SysprofPage   *self,
                               GAsyncResult  *result,
                               GError       **error)
{
  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
sysprof_page_finalize (GObject *object)
{
  SysprofPage *self = (SysprofPage *)object;
  SysprofPagePrivate *priv = sysprof_page_get_instance_private (self);

  g_clear_pointer (&priv->title, g_free);

  G_OBJECT_CLASS (sysprof_page_parent_class)->finalize (object);
}

static void
sysprof_page_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  SysprofPage *self = SYSPROF_PAGE (object);

  switch (prop_id)
    {
    case PROP_TITLE:
      g_value_set_string (value, sysprof_page_get_title (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_page_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  SysprofPage *self = SYSPROF_PAGE (object);

  switch (prop_id)
    {
    case PROP_TITLE:
      sysprof_page_set_title (self, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_page_class_init (SysprofPageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_page_finalize;
  object_class->get_property = sysprof_page_get_property;
  object_class->set_property = sysprof_page_set_property;

  klass->load_async = sysprof_page_real_load_async;
  klass->load_finish = sysprof_page_real_load_finish;

  properties [PROP_TITLE] =
    g_param_spec_string ("title",
                         "Title",
                         "The title for the page",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_page_init (SysprofPage *self)
{
}

/**
 * sysprof_page_load_async:
 * @condition: (nullable): a #sysprofCaptureCondition or %NULL
 * @cancellable: (nullable): a #GCancellable or %NULL
 *
 * Since: 3.34
 */
void
sysprof_page_load_async (SysprofPage             *self,
                         SysprofCaptureReader    *reader,
                         SysprofSelection        *selection,
                         SysprofCaptureCondition *condition,
                         GCancellable            *cancellable,
                         GAsyncReadyCallback      callback,
                         gpointer                 user_data)
{
  g_return_if_fail (SYSPROF_IS_PAGE (self));
  g_return_if_fail (reader != NULL);
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  SYSPROF_PAGE_GET_CLASS (self)->load_async (self, reader, selection, condition, cancellable, callback, user_data);
}

gboolean
sysprof_page_load_finish (SysprofPage   *self,
                          GAsyncResult  *result,
                          GError       **error)
{
  g_return_val_if_fail (SYSPROF_IS_PAGE (self), FALSE);
  g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

  return SYSPROF_PAGE_GET_CLASS (self)->load_finish (self, result, error);
}

void
sysprof_page_set_size_group (SysprofPage  *self,
                             GtkSizeGroup *size_group)
{
  g_return_if_fail (SYSPROF_IS_PAGE (self));
  g_return_if_fail (!size_group || GTK_IS_SIZE_GROUP (size_group));

  if (SYSPROF_PAGE_GET_CLASS (self)->set_size_group)
    SYSPROF_PAGE_GET_CLASS (self)->set_size_group (self, size_group);
}

void
sysprof_page_set_hadjustment (SysprofPage   *self,
                              GtkAdjustment *hadjustment)
{
  g_return_if_fail (SYSPROF_IS_PAGE (self));
  g_return_if_fail (!hadjustment || GTK_IS_ADJUSTMENT (hadjustment));

  if (SYSPROF_PAGE_GET_CLASS (self)->set_hadjustment)
    SYSPROF_PAGE_GET_CLASS (self)->set_hadjustment (self, hadjustment);
}

void
sysprof_page_reload (SysprofPage *self)
{
  GtkWidget *display;

  g_return_if_fail (SYSPROF_IS_PAGE (self));

  if ((display = gtk_widget_get_ancestor (GTK_WIDGET (self), SYSPROF_TYPE_DISPLAY)))
    _sysprof_display_reload_page (SYSPROF_DISPLAY (display), self);
}
