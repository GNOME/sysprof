/* sp-model-filter.h
 *
 * Copyright (C) 2016 Christian Hergert <christian@hergert.me>
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

#ifndef SP_MODEL_FILTER_H
#define SP_MODEL_FILTER_H

#include <gio/gio.h>

G_BEGIN_DECLS

#define SP_TYPE_MODEL_FILTER (sp_model_filter_get_type())

typedef gboolean (*SpModelFilterFunc) (GObject  *object,
                                       gpointer  user_data);

G_DECLARE_DERIVABLE_TYPE (SpModelFilter, sp_model_filter, SP, MODEL_FILTER, GObject)

struct _SpModelFilterClass
{
  GObjectClass parent_class;
};

SpModelFilter *sp_model_filter_new             (GListModel        *child_model);
GListModel    *sp_model_filter_get_child_model (SpModelFilter     *self);
void           sp_model_filter_invalidate      (SpModelFilter     *self);
void           sp_model_filter_set_filter_func (SpModelFilter     *self,
                                                SpModelFilterFunc  filter_func,
                                                gpointer           filter_func_data,
                                                GDestroyNotify     filter_func_data_destroy);

G_END_DECLS

#endif /* SP_MODEL_FILTER_H */
