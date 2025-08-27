/*
 * sysprof-marks-section-model-item.h
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

#include <glib-object.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_MARKS_SECTION_MODEL_ITEM (sysprof_marks_section_model_item_get_type())
G_DECLARE_FINAL_TYPE (SysprofMarksSectionModelItem, sysprof_marks_section_model_item, SYSPROF, MARKS_SECTION_MODEL_ITEM, GObject)

SysprofMarksSectionModelItem *sysprof_marks_section_model_item_new (gpointer item);

gpointer sysprof_marks_section_model_item_get_item    (SysprofMarksSectionModelItem *self);

gboolean sysprof_marks_section_model_item_get_visible (SysprofMarksSectionModelItem *self);
void     sysprof_marks_section_model_item_set_visible (SysprofMarksSectionModelItem *self,
                                                       gboolean                      visible);

G_END_DECLS
