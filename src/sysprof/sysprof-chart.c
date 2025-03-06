/* sysprof-chart.c
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
#include "sysprof-chart.h"
#include "sysprof-chart-layer-factory-private.h"
#include "sysprof-chart-layer-item.h"
#include "sysprof-chart-layer-item-widget.h"

typedef struct
{
  SysprofSession *session;
  char *title;

  SysprofChartLayerFactory *factory;
  GListModel *model;

  double motion_x;
  double motion_y;

  guint pointer_in_chart : 1;
} SysprofChartPrivate;

enum {
  PROP_0,
  PROP_FACTORY,
  PROP_MODEL,
  PROP_SESSION,
  PROP_TITLE,
  N_PROPS
};

enum {
  ACTIVATE_LAYER_ITEM,
  N_SIGNALS
};

G_DEFINE_TYPE_WITH_PRIVATE (SysprofChart, sysprof_chart, GTK_TYPE_WIDGET)

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];


static void
sysprof_chart_motion_enter_cb (SysprofChart             *self,
                               double                    x,
                               double                    y,
                               GtkEventControllerMotion *motion)
{
  SysprofChartPrivate *priv = sysprof_chart_get_instance_private (self);

  g_assert (SYSPROF_IS_CHART (self));
  g_assert (GTK_IS_EVENT_CONTROLLER_MOTION (motion));

  priv->motion_x = x;
  priv->motion_y = y;
  priv->pointer_in_chart = TRUE;

  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
sysprof_chart_motion_cb (SysprofChart             *self,
                         double                    x,
                         double                    y,
                         GtkEventControllerMotion *motion)
{
  SysprofChartPrivate *priv = sysprof_chart_get_instance_private (self);

  g_assert (SYSPROF_IS_CHART (self));
  g_assert (GTK_IS_EVENT_CONTROLLER_MOTION (motion));

  priv->motion_x = x;
  priv->motion_y = y;

  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
sysprof_chart_motion_leave_cb (SysprofChart             *self,
                               GtkEventControllerMotion *motion)
{
  SysprofChartPrivate *priv = sysprof_chart_get_instance_private (self);

  g_assert (SYSPROF_IS_CHART (self));
  g_assert (GTK_IS_EVENT_CONTROLLER_MOTION (motion));

  priv->motion_x = -1;
  priv->motion_y = -1;
  priv->pointer_in_chart = FALSE;

  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
sysprof_chart_size_allocate (GtkWidget *widget,
                             int        width,
                             int        height,
                             int        baseline)
{
  g_assert (SYSPROF_IS_CHART (widget));

  GTK_WIDGET_CLASS (sysprof_chart_parent_class)->size_allocate (widget, width, height, baseline);

  for (GtkWidget *child = gtk_widget_get_first_child (widget);
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    gtk_widget_size_allocate (child,
                              &(GtkAllocation) {0, 0, width, height},
                              baseline);
}

static void
sysprof_chart_snapshot (GtkWidget   *widget,
                        GtkSnapshot *snapshot)
{
  SysprofChart *self = (SysprofChart *)widget;
  SysprofChartPrivate *priv = sysprof_chart_get_instance_private (self);
  graphene_point_t child_origin;

  g_assert (SYSPROF_IS_CHART (self));

  GTK_WIDGET_CLASS (sysprof_chart_parent_class)->snapshot (widget, snapshot);

  if (priv->pointer_in_chart)
    {
      GtkWidget *pick = gtk_widget_pick (widget, priv->motion_x, priv->motion_y, GTK_PICK_DEFAULT);

      if (SYSPROF_IS_CHART_LAYER (pick) &&
          gtk_widget_compute_point (pick, widget, &GRAPHENE_POINT_INIT (0, 0), &child_origin))
        {
          gtk_snapshot_translate (snapshot, &child_origin);

          sysprof_chart_layer_snapshot_motion (SYSPROF_CHART_LAYER (pick),
                                               snapshot,
                                               priv->motion_x,
                                               priv->motion_y);

          child_origin.x *= -1;
          child_origin.y *= -1;
          gtk_snapshot_translate (snapshot, &child_origin);
        }
    }
}

static gboolean
sysprof_chart_click_pressed_cb (SysprofChart    *self,
                                int              n_presses,
                                double           x,
                                double           y,
                                GtkGestureClick *click)
{
  g_autoptr(GObject) item = NULL;
  GtkWidget *pick;
  gboolean ret = GDK_EVENT_PROPAGATE;

  g_assert (SYSPROF_IS_CHART (self));
  g_assert (GTK_IS_GESTURE_CLICK (click));

  if (n_presses != 1)
    return GDK_EVENT_PROPAGATE;

  if ((pick = gtk_widget_pick (GTK_WIDGET (self), x, y, GTK_PICK_DEFAULT)) &&
      SYSPROF_IS_CHART_LAYER (pick) &&
      (item = sysprof_chart_layer_lookup_item (SYSPROF_CHART_LAYER (pick), x, y)))
    g_signal_emit (self, signals[ACTIVATE_LAYER_ITEM], 0, pick, item, &ret);

  return ret;
}

static void
sysprof_chart_measure (GtkWidget      *widget,
                       GtkOrientation  orientation,
                       int             for_size,
                       int            *minimum,
                       int            *natural,
                       int            *minium_baseline,
                       int            *natural_baseline)
{
  *minium_baseline = -1;
  *natural_baseline = -1;

  for (GtkWidget *child = gtk_widget_get_first_child (widget);
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      int child_min;
      int child_nat;

      gtk_widget_measure (child, orientation, for_size, &child_min, &child_nat, NULL, NULL);

      *minimum = MAX (*minimum, child_min);
      *natural = MAX (*natural, child_nat);
    }
}

static void
sysprof_chart_dispose (GObject *object)
{
  SysprofChart *self = (SysprofChart *)object;
  SysprofChartPrivate *priv = sysprof_chart_get_instance_private (self);
  GtkWidget *child;

  sysprof_chart_set_model (self, NULL);

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self))))
    gtk_widget_unparent (child);

  g_clear_object (&priv->session);
  g_clear_pointer (&priv->title, g_free);

  G_OBJECT_CLASS (sysprof_chart_parent_class)->dispose (object);
}

static void
sysprof_chart_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  SysprofChart *self = SYSPROF_CHART (object);
  SysprofChartPrivate *priv = sysprof_chart_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_FACTORY:
      g_value_set_object (value, priv->factory);
      break;

    case PROP_MODEL:
      g_value_set_object (value, sysprof_chart_get_model (self));
      break;

    case PROP_SESSION:
      g_value_set_object (value, sysprof_chart_get_session (self));
      break;

    case PROP_TITLE:
      g_value_set_string (value, sysprof_chart_get_title (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_chart_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  SysprofChart *self = SYSPROF_CHART (object);
  SysprofChartPrivate *priv = sysprof_chart_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_FACTORY:
      priv->factory = g_value_dup_object (value);
      break;

    case PROP_MODEL:
      sysprof_chart_set_model (self, g_value_get_object (value));
      break;

    case PROP_SESSION:
      sysprof_chart_set_session (self, g_value_get_object (value));
      break;

    case PROP_TITLE:
      sysprof_chart_set_title (self, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_chart_class_init (SysprofChartClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_chart_dispose;
  object_class->get_property = sysprof_chart_get_property;
  object_class->set_property = sysprof_chart_set_property;

  widget_class->measure = sysprof_chart_measure;
  widget_class->size_allocate = sysprof_chart_size_allocate;
  widget_class->snapshot = sysprof_chart_snapshot;

  properties [PROP_MODEL] =
    g_param_spec_object ("model", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_FACTORY] =
    g_param_spec_object ("factory", NULL, NULL,
                         SYSPROF_TYPE_CHART_LAYER_FACTORY,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_SESSION] =
    g_param_spec_object ("session", NULL, NULL,
                         G_TYPE_OBJECT,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_TITLE] =
    g_param_spec_string ("title", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  signals[ACTIVATE_LAYER_ITEM] =
    g_signal_new ("activate-layer-item",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (SysprofChartClass, activate_layer_item),
                  g_signal_accumulator_true_handled, NULL,
                  NULL,
                  G_TYPE_BOOLEAN,
                  2,
                  SYSPROF_TYPE_CHART_LAYER,
                  G_TYPE_OBJECT);

  gtk_widget_class_set_css_name (widget_class, "chart");

  g_type_ensure (SYSPROF_TYPE_CHART_LAYER_FACTORY);
  g_type_ensure (SYSPROF_TYPE_CHART_LAYER_ITEM);
  g_type_ensure (SYSPROF_TYPE_CHART_LAYER_ITEM_WIDGET);
}

static void
sysprof_chart_init (SysprofChart *self)
{
  SysprofChartPrivate *priv = sysprof_chart_get_instance_private (self);
  GtkEventController *motion;
  GtkEventController *click;

  priv->motion_x = -1;
  priv->motion_y = -1;

  _sysprof_css_init ();

  motion = gtk_event_controller_motion_new ();
  g_signal_connect_object (motion,
                           "enter",
                           G_CALLBACK (sysprof_chart_motion_enter_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (motion,
                           "leave",
                           G_CALLBACK (sysprof_chart_motion_leave_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (motion,
                           "motion",
                           G_CALLBACK (sysprof_chart_motion_cb),
                           self,
                           G_CONNECT_SWAPPED);
  gtk_widget_add_controller (GTK_WIDGET (self), g_steal_pointer (&motion));

  click = GTK_EVENT_CONTROLLER (gtk_gesture_click_new ());
  g_signal_connect_object (click,
                           "pressed",
                           G_CALLBACK (sysprof_chart_click_pressed_cb),
                           self,
                           G_CONNECT_SWAPPED);
  gtk_widget_add_controller (GTK_WIDGET (self), g_steal_pointer (&click));
}

const char *
sysprof_chart_get_title (SysprofChart *self)
{
  SysprofChartPrivate *priv = sysprof_chart_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_CHART (self), NULL);

  return priv->title;
}

/**
 * sysprof_chart_get_session:
 * @self: a #SysprofChart
 *
 * Gets the #SysprofSession of the chart.
 *
 * Returns: (transfer none) (nullable): a #SysprofSession or %NULL
 */
SysprofSession *
sysprof_chart_get_session (SysprofChart *self)
{
  SysprofChartPrivate *priv = sysprof_chart_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_CHART (self), NULL);

  return priv->session;
}

/**
 * sysprof_chart_set_session:
 * @self: a #SysprofChart
 * @session: (nullable): a #SysprofSession
 *
 * Sets the session for the chart.
 */
void
sysprof_chart_set_session (SysprofChart   *self,
                           SysprofSession *session)
{
  SysprofChartPrivate *priv = sysprof_chart_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_CHART (self));
  g_return_if_fail (!session || SYSPROF_IS_SESSION (session));

  if (g_set_object (&priv->session, session))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SESSION]);
}

void
sysprof_chart_set_title (SysprofChart *self,
                         const char   *title)
{
  SysprofChartPrivate *priv = sysprof_chart_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_CHART (self));

  if (g_set_str (&priv->title, title))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
}

void
sysprof_chart_add_layer (SysprofChart      *self,
                         SysprofChartLayer *layer)
{
  g_return_if_fail (SYSPROF_IS_CHART (self));
  g_return_if_fail (SYSPROF_IS_CHART_LAYER (layer));
  g_return_if_fail (gtk_widget_get_parent (GTK_WIDGET (layer)) == NULL);

  gtk_widget_set_parent (GTK_WIDGET (layer), GTK_WIDGET (self));
}

void
sysprof_chart_remove_layer (SysprofChart      *self,
                            SysprofChartLayer *layer)
{
  g_return_if_fail (SYSPROF_IS_CHART (self));
  g_return_if_fail (SYSPROF_IS_CHART_LAYER (layer));
  g_return_if_fail (gtk_widget_get_parent (GTK_WIDGET (layer)) == GTK_WIDGET (self));

  gtk_widget_unparent (GTK_WIDGET (layer));
}


GListModel *
sysprof_chart_get_model (SysprofChart *self)
{
  SysprofChartPrivate *priv = sysprof_chart_get_instance_private (self);

  g_return_val_if_fail (SYSPROF_IS_CHART (self), NULL);

  return priv->model;
}

static void
sysprof_chart_items_changed_cb (SysprofChart *self,
                                guint         position,
                                guint         removed,
                                guint         added,
                                GListModel   *model)
{
  SysprofChartPrivate *priv = sysprof_chart_get_instance_private (self);

  g_assert (SYSPROF_IS_CHART (self));
  g_assert (G_IS_LIST_MODEL (model));

  if (removed > 0)
    {
      GtkWidget *child = gtk_widget_get_first_child (GTK_WIDGET (self));

      for (guint i = 1; i <= position; i++)
        child = gtk_widget_get_next_sibling (child);

      for (guint i = 0; i < removed; i++)
        {
          GtkWidget *target = child;
          child = gtk_widget_get_next_sibling (child);
          gtk_widget_unparent (target);
        }
    }

  if (added > 0)
    {
      GtkWidget *child = gtk_widget_get_first_child (GTK_WIDGET (self));

      for (guint i = 1; i < position; i++)
        child = gtk_widget_get_next_sibling (child);

      for (guint i = 0; i < added; i++)
        {
          g_autoptr(GObject) item = g_list_model_get_item (model, position + i);
          g_autoptr(SysprofChartLayerItem) layer_item = NULL;
          SysprofChartLayerItemWidget *widget;

          layer_item = g_object_new (SYSPROF_TYPE_CHART_LAYER_ITEM,
                                     "item", item,
                                     NULL);
          if (priv->factory)
            _sysprof_chart_layer_factory_setup (priv->factory, layer_item);
          widget = sysprof_chart_layer_item_widget_new (layer_item);
          gtk_widget_insert_after (GTK_WIDGET (widget), GTK_WIDGET (self), child);
          child = GTK_WIDGET (widget);
        }
    }
}

void
sysprof_chart_set_model (SysprofChart *self,
                         GListModel   *model)
{
  SysprofChartPrivate *priv = sysprof_chart_get_instance_private (self);

  g_return_if_fail (SYSPROF_IS_CHART (self));

  if (priv->model == model)
    return;

  if (model)
    g_object_ref (model);

  if (priv->model)
    {
      GtkWidget *child;

      g_signal_handlers_disconnect_by_func (priv->model,
                                            G_CALLBACK (sysprof_chart_items_changed_cb),
                                            self);

      while ((child = gtk_widget_get_first_child (GTK_WIDGET (self))))
        gtk_widget_unparent (child);

      g_clear_object (&priv->model);
    }

  priv->model = model;

  if (model)
    {
      guint n_items = g_list_model_get_n_items (model);
      g_signal_connect_object (priv->model,
                               "items-changed",
                               G_CALLBACK (sysprof_chart_items_changed_cb),
                               self,
                               G_CONNECT_SWAPPED);
      if (n_items)
        sysprof_chart_items_changed_cb (self, 0, 0, n_items, model);
    }

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_MODEL]);
}
