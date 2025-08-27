/*
 * sysprof-expression-map-model.h
 *
 * Copyright 2025 Georges Basile Stavracas Neto <feaneron@igalia.com>
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

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_EXPRESSION_MAP_MODEL (sysprof_expression_map_model_get_type())
G_DECLARE_FINAL_TYPE (SysprofExpressionMapModel, sysprof_expression_map_model, SYSPROF, EXPRESSION_MAP_MODEL, GObject)

SysprofExpressionMapModel *sysprof_expression_map_model_new (GListModel    *model,
                                                             GtkExpression *expression);

GListModel    *sysprof_expression_map_model_get_model      (SysprofExpressionMapModel *self);
void           sysprof_expression_map_model_set_model      (SysprofExpressionMapModel *self,
                                                            GListModel                *model);

GtkExpression *sysprof_expression_map_model_get_expression (SysprofExpressionMapModel *self);
void           sysprof_expression_map_model_set_expression (SysprofExpressionMapModel *self,
                                                            GtkExpression             *expression);

G_END_DECLS
