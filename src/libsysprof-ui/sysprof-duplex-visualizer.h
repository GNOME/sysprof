/* sysprof-duplex-visualizer.h
 *
 * Copyright 2019 Christian Hergert <christian@hergert.me>
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

#include "sysprof-visualizer.h"

G_BEGIN_DECLS

#define SYSPROF_TYPE_DUPLEX_VISUALIZER (sysprof_duplex_visualizer_get_type())

G_DECLARE_FINAL_TYPE (SysprofDuplexVisualizer, sysprof_duplex_visualizer, SYSPROF, DUPLEX_VISUALIZER, SysprofVisualizer)

GtkWidget *sysprof_duplex_visualizer_new          (void);
gboolean   sysprof_duplex_visualizer_get_use_diff (SysprofDuplexVisualizer *self);
void       sysprof_duplex_visualizer_set_use_diff (SysprofDuplexVisualizer *self,
                                                   gboolean                 use_diff);
void       sysprof_duplex_visualizer_set_labels   (SysprofDuplexVisualizer *self,
                                                   const gchar             *rx_label,
                                                   const gchar             *tx_label);
void       sysprof_duplex_visualizer_set_counters (SysprofDuplexVisualizer *self,
                                                   guint                    rx_counter,
                                                   guint                    tx_counter);
void       sysprof_duplex_visualizer_set_colors   (SysprofDuplexVisualizer *self,
                                                   const GdkRGBA           *rx_rgba,
                                                   const GdkRGBA           *tx_rgba);

G_END_DECLS
