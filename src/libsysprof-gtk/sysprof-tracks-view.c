/* sysprof-tracks-view.c
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

#include "sysprof-css-private.h"
#include "sysprof-track-view.h"
#include "sysprof-tracks-view.h"

struct _SysprofTracksView
{
  GtkWidget       parent_instance;

  SysprofSession *session;

  GtkListView    *list_view;
};

enum {
  PROP_0,
  PROP_SESSION,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofTracksView, sysprof_tracks_view, GTK_TYPE_WIDGET)

static GParamSpec *properties [N_PROPS];

static void
sysprof_tracks_view_dispose (GObject *object)
{
  SysprofTracksView *self = (SysprofTracksView *)object;
  GtkWidget *child;

  g_clear_object (&self->session);

  gtk_widget_dispose_template (GTK_WIDGET (self), SYSPROF_TYPE_TRACKS_VIEW);

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self))))
    gtk_widget_unparent (child);

  G_OBJECT_CLASS (sysprof_tracks_view_parent_class)->dispose (object);
}

static void
sysprof_tracks_view_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  SysprofTracksView *self = SYSPROF_TRACKS_VIEW (object);

  switch (prop_id)
    {
    case PROP_SESSION:
      g_value_set_object (value, sysprof_tracks_view_get_session (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_tracks_view_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  SysprofTracksView *self = SYSPROF_TRACKS_VIEW (object);

  switch (prop_id)
    {
    case PROP_SESSION:
      sysprof_tracks_view_set_session (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_tracks_view_class_init (SysprofTracksViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_tracks_view_dispose;
  object_class->get_property = sysprof_tracks_view_get_property;
  object_class->set_property = sysprof_tracks_view_set_property;

  properties[PROP_SESSION] =
    g_param_spec_object ("session", NULL, NULL,
                         SYSPROF_TYPE_SESSION,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/libsysprof-gtk/sysprof-tracks-view.ui");
  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_bind_template_child (widget_class, SysprofTracksView, list_view);

  g_type_ensure (SYSPROF_TYPE_TRACK_VIEW);
}

static void
sysprof_tracks_view_init (SysprofTracksView *self)
{
  _sysprof_css_init ();

  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
sysprof_tracks_view_new (void)
{
  return g_object_new (SYSPROF_TYPE_TRACKS_VIEW, NULL);
}

/**
 * sysprof_tracks_view_get_session:
 * @self: a #SysprofTracksView
 *
 * Gets the session for the tracks.
 *
 * Returns: (transfer none) (nullable): a #SysprofSession or %NULL
 */
SysprofSession *
sysprof_tracks_view_get_session (SysprofTracksView *self)
{
  g_return_val_if_fail (SYSPROF_IS_TRACKS_VIEW (self), NULL);

  return self->session;
}

static GListModel *
sysprof_tracks_view_create_model_func (gpointer item,
                                       gpointer user_data)
{
  /* TODO: allow tracks to have sub-tracks */

  return NULL;
}

void
sysprof_tracks_view_set_session (SysprofTracksView *self,
                                 SysprofSession    *session)
{
  g_return_if_fail (SYSPROF_IS_TRACKS_VIEW (self));
  g_return_if_fail (!session || SYSPROF_IS_SESSION (session));

  if (self->session == session)
    return;

  if (self->session)
    {
      gtk_list_view_set_model (self->list_view, NULL);
      g_clear_object (&self->session);
    }

  if (session)
    {
      g_autoptr(GtkTreeListModel) tree_list_model = NULL;
      g_autoptr(GtkNoSelection) no = NULL;
      g_autoptr(GListModel) tracks = NULL;

      self->session = g_object_ref (session);

      tracks = sysprof_session_list_tracks (session);
      tree_list_model = gtk_tree_list_model_new (g_object_ref (tracks),
                                                 FALSE,
                                                 FALSE,
                                                 sysprof_tracks_view_create_model_func,
                                                 self, NULL);
      no = gtk_no_selection_new (g_object_ref (G_LIST_MODEL (tree_list_model)));

      gtk_list_view_set_model (self->list_view, GTK_SELECTION_MODEL (no));
    }

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SESSION]);
}
