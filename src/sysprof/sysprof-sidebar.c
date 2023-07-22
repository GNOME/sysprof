/* sysprof-sidebar.c
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

#include "sysprof-section.h"
#include "sysprof-sidebar.h"

struct _SysprofSidebar
{
  GtkWidget     parent_instance;
  GSignalGroup *stack_signals;
  GtkListBox   *list_box;
};

enum {
  PROP_0,
  PROP_STACK,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofSidebar, sysprof_sidebar, GTK_TYPE_WIDGET)

static GParamSpec *properties [N_PROPS];

static gboolean
string_to_boolean (GBinding     *binding,
                   const GValue *from_value,
                   GValue       *to_value,
                   gpointer      user_data)
{
  g_value_set_boolean (to_value, !!g_value_get_string (from_value));
  return TRUE;
}

static GtkWidget *
sysprof_sidebar_create_row (gpointer item,
                            gpointer user_data)
{
  GtkStackPage *page = item;
  SysprofSidebar *sidebar = user_data;
  SysprofSection *section;
  GtkListBoxRow *row;
  GtkLabel *indicator;
  GtkImage *image;
  GtkLabel *label;
  GtkBox *box;

  g_assert (GTK_IS_STACK_PAGE (page));
  g_assert (SYSPROF_IS_SIDEBAR (sidebar));

  section = SYSPROF_SECTION (gtk_stack_page_get_child (page));

  box = g_object_new (GTK_TYPE_BOX,
                      "orientation", GTK_ORIENTATION_HORIZONTAL,
                      "spacing", 6,
                      NULL);
  label = g_object_new (GTK_TYPE_LABEL,
                        "xalign", .0f,
                        "label", sysprof_section_get_title (section),
                        "hexpand", TRUE,
                        NULL);
  image = g_object_new (GTK_TYPE_IMAGE,
                        "icon-name", sysprof_section_get_icon_name (section),
                        NULL);
  indicator = g_object_new (GTK_TYPE_LABEL,
                            "css-classes", (const char * const[]) {"indicator", NULL},
                            "valign", GTK_ALIGN_CENTER,
                            NULL);
  gtk_box_append (box, GTK_WIDGET (image));
  gtk_box_append (box, GTK_WIDGET (label));
  gtk_box_append (box, GTK_WIDGET (indicator));

  g_object_bind_property (section, "indicator",
                          indicator, "label",
                          G_BINDING_SYNC_CREATE);
  g_object_bind_property_full (section, "indicator",
                               indicator, "visible",
                               G_BINDING_SYNC_CREATE,
                               string_to_boolean, NULL, NULL, NULL);

  row = g_object_new (GTK_TYPE_LIST_BOX_ROW,
                      "child", box,
                      NULL);
  g_object_set_data_full (G_OBJECT (row), "SECTION", g_object_ref (section), g_object_unref);

  return GTK_WIDGET (row);
}

static void
sysprof_sidebar_bind_cb (SysprofSidebar *self,
                         GtkStack       *stack,
                         GSignalGroup   *group)
{
  GtkSelectionModel *model;

  g_assert (SYSPROF_IS_SIDEBAR (self));
  g_assert (GTK_IS_STACK (stack));
  g_assert (G_IS_SIGNAL_GROUP (group));

  model = gtk_stack_get_pages (stack);

  gtk_list_box_bind_model (self->list_box,
                           G_LIST_MODEL (model),
                           sysprof_sidebar_create_row,
                           self,
                           NULL);

  gtk_list_box_select_row (self->list_box, gtk_list_box_get_row_at_index (self->list_box, 0));
}

static void
sysprof_sidebar_unbind_cb (SysprofSidebar *self,
                           GSignalGroup   *group)
{
  g_assert (SYSPROF_IS_SIDEBAR (self));
  g_assert (G_IS_SIGNAL_GROUP (group));

  gtk_list_box_bind_model (self->list_box, NULL, NULL, NULL, NULL);
}

static void
list_box_row_activated_cb (SysprofSidebar *self,
                           GtkListBoxRow  *row,
                           GtkListBox     *list_box)
{
  SysprofSection *section;
  g_autoptr(GtkStack) stack = NULL;

  g_assert (SYSPROF_IS_SIDEBAR (self));
  g_assert (GTK_IS_LIST_BOX_ROW (row));
  g_assert (GTK_IS_LIST_BOX (list_box));

  if ((section = g_object_get_data (G_OBJECT (row), "SECTION")) &&
      SYSPROF_IS_SECTION (section) &&
      (stack = g_signal_group_dup_target (self->stack_signals)))
    gtk_stack_set_visible_child (stack, GTK_WIDGET (section));
}

static void
sysprof_sidebar_header_func (GtkListBoxRow *row,
                             GtkListBoxRow *before_row,
                             gpointer       user_data)
{
  SysprofSection *section;
  SysprofSection *before_section;

  if (before_row == NULL)
    return;

  if ((section = g_object_get_data (G_OBJECT (row), "SECTION")) &&
      (before_section = g_object_get_data (G_OBJECT (before_row), "SECTION")) &&
      g_strcmp0 (sysprof_section_get_category (section),
                 sysprof_section_get_category (before_section)) != 0)
    gtk_list_box_row_set_header (row, gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));
}

static void
sysprof_sidebar_dispose (GObject *object)
{
  SysprofSidebar *self = (SysprofSidebar *)object;
  GtkWidget *child;

  g_clear_object (&self->stack_signals);

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self))))
    gtk_widget_unparent (child);

  G_OBJECT_CLASS (sysprof_sidebar_parent_class)->dispose (object);
}

static void
sysprof_sidebar_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  SysprofSidebar *self = SYSPROF_SIDEBAR (object);

  switch (prop_id)
    {
    case PROP_STACK:
      g_value_take_object (value, g_signal_group_dup_target (self->stack_signals));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_sidebar_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  SysprofSidebar *self = SYSPROF_SIDEBAR (object);

  switch (prop_id)
    {
    case PROP_STACK:
      g_signal_group_set_target (self->stack_signals, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_sidebar_class_init (SysprofSidebarClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = sysprof_sidebar_dispose;
  object_class->get_property = sysprof_sidebar_get_property;
  object_class->set_property = sysprof_sidebar_set_property;

  properties[PROP_STACK] =
    g_param_spec_object ("stack", NULL, NULL,
                         GTK_TYPE_STACK,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-sidebar.ui");
  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_bind_template_child (widget_class, SysprofSidebar, list_box);
  gtk_widget_class_bind_template_callback (widget_class, list_box_row_activated_cb);
}

static void
sysprof_sidebar_init (SysprofSidebar *self)
{
  self->stack_signals = g_signal_group_new (GTK_TYPE_STACK);
  g_signal_connect_object (self->stack_signals,
                           "bind",
                           G_CALLBACK (sysprof_sidebar_bind_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->stack_signals,
                           "unbind",
                           G_CALLBACK (sysprof_sidebar_unbind_cb),
                           self,
                           G_CONNECT_SWAPPED);

  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_list_box_set_header_func (self->list_box,
                                sysprof_sidebar_header_func,
                                NULL, NULL);
}
