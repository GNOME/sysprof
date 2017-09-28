/* sp-recording-state-view.h
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

#ifndef SP_RECORDING_STATE_VIEW_H
#define SP_RECORDING_STATE_VIEW_H

#include <gtk/gtk.h>

#include "profiler/sp-profiler.h"

G_BEGIN_DECLS

#define SP_TYPE_RECORDING_STATE_VIEW (sp_recording_state_view_get_type())

G_DECLARE_DERIVABLE_TYPE (SpRecordingStateView, sp_recording_state_view, SP, RECORDING_STATE_VIEW, GtkBin)

struct _SpRecordingStateViewClass
{
  GtkBinClass parent;

  gpointer padding[4];
};

GtkWidget *sp_recording_state_view_new          (void);
void       sp_recording_state_view_set_profiler (SpRecordingStateView *self,
                                                 SpProfiler           *profiler);

G_END_DECLS

#endif /* SP_RECORDING_STATE_VIEW_H */
