/* sysprof-animation.h
 *
 * Copyright (C) 2010-2020 Christian Hergert <christian@hergert.me>
 *
 * This file is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2.1 of the License, or (at your option)
 * any later version.
 *
 * This file is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <gdk/gdk.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_ANIMATION      (sysprof_animation_get_type())
#define SYSPROF_TYPE_ANIMATION_MODE (sysprof_animation_mode_get_type())

G_DECLARE_FINAL_TYPE (SysprofAnimation, sysprof_animation, SYSPROF, ANIMATION, GInitiallyUnowned)

typedef enum _SysprofAnimationMode
{
  SYSPROF_ANIMATION_LINEAR,
  SYSPROF_ANIMATION_EASE_IN_QUAD,
  SYSPROF_ANIMATION_EASE_OUT_QUAD,
  SYSPROF_ANIMATION_EASE_IN_OUT_QUAD,
  SYSPROF_ANIMATION_EASE_IN_CUBIC,
  SYSPROF_ANIMATION_EASE_OUT_CUBIC,
  SYSPROF_ANIMATION_EASE_IN_OUT_CUBIC,
  SYSPROF_ANIMATION_LAST
} SysprofAnimationMode;

GType            sysprof_animation_mode_get_type      (void) G_GNUC_CONST;
void             sysprof_animation_start              (SysprofAnimation     *animation);
void             sysprof_animation_stop               (SysprofAnimation     *animation);
void             sysprof_animation_add_property       (SysprofAnimation     *animation,
                                                       GParamSpec           *pspec,
                                                       const GValue         *value);
SysprofAnimation *sysprof_object_animatev             (gpointer              object,
                                                       SysprofAnimationMode  mode,
                                                       guint                 duration_msec,
                                                       GdkFrameClock        *frame_clock,
                                                       const gchar          *first_property,
                                                       va_list               args);
SysprofAnimation *sysprof_object_animate              (gpointer              object,
                                                       SysprofAnimationMode  mode,
                                                       guint                 duration_msec,
                                                       GdkFrameClock        *frame_clock,
                                                       const gchar          *first_property,
                                                       ...) G_GNUC_NULL_TERMINATED;
SysprofAnimation *sysprof_object_animate_full         (gpointer              object,
                                                       SysprofAnimationMode  mode,
                                                       guint                 duration_msec,
                                                       GdkFrameClock        *frame_clock,
                                                       GDestroyNotify        notify,
                                                       gpointer              notify_data,
                                                       const gchar          *first_property,
                                                       ...) G_GNUC_NULL_TERMINATED;
guint            sysprof_animation_calculate_duration (GdkMonitor           *monitor,
                                                       gdouble               from_value,
                                                       gdouble               to_value);

G_END_DECLS

