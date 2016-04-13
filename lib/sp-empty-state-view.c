/* sp-empty-state-view.c
 *
 * Copyright (C) 2016 Christian Hergert <chergert@redhat.com>
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

#include "sp-empty-state-view.h"

G_DEFINE_TYPE (SpEmptyStateView, sp_empty_state_view, GTK_TYPE_BIN)

GtkWidget *
sp_empty_state_view_new (void)
{
  return g_object_new (SP_TYPE_EMPTY_STATE_VIEW, NULL);
}

static void
sp_empty_state_view_class_init (SpEmptyStateViewClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/ui/sp-empty-state-view.ui");
}

static void
sp_empty_state_view_init (SpEmptyStateView *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}
