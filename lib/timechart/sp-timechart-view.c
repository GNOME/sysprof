/* sp-timechart-view.c
 *
 * Copyright 2018 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "sp-timechart-view"

#include "sp-timechart-view.h"

struct _SpTimechartView
{
  GtkBin parent_instance;
};

enum {
  PROP_0,
  N_PROPS
};

G_DEFINE_TYPE (SpTimechartView, sp_timechart_view, GTK_TYPE_BIN)

static GParamSpec *properties [N_PROPS];

static void
sp_timechart_view_finalize (GObject *object)
{
  SpTimechartView *self = (SpTimechartView *)object;

  G_OBJECT_CLASS (sp_timechart_view_parent_class)->finalize (object);
}

static void
sp_timechart_view_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  SpTimechartView *self = SP_TIMECHART_VIEW (object);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_timechart_view_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  SpTimechartView *self = SP_TIMECHART_VIEW (object);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sp_timechart_view_class_init (SpTimechartViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = sp_timechart_view_finalize;
  object_class->get_property = sp_timechart_view_get_property;
  object_class->set_property = sp_timechart_view_set_property;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/sysprof/ui/sp-timechart-view.ui");
}

static void
sp_timechart_view_init (SpTimechartView *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
sp_timechart_view_new (void)
{
  return g_object_new (SP_TYPE_TIMECHART_VIEW, NULL);
}
