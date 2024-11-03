/* sysprof.h
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

#pragma once

#include <glib.h>

G_BEGIN_DECLS

#define SYSPROF_INSIDE
# include "sysprof-battery-charge.h"
# include "sysprof-bundled-symbolizer.h"
# include "sysprof-callgraph-frame.h"
# include "sysprof-callgraph-symbol.h"
# include "sysprof-callgraph.h"
# include "sysprof-category-summary.h"
# include "sysprof-cpu-info.h"
# include "sysprof-cpu-usage.h"
# include "sysprof-dbus-monitor.h"
# include "sysprof-debuginfod-symbolizer.h"
# include "sysprof-diagnostic.h"
# include "sysprof-disk-usage.h"
# include "sysprof-document-allocation.h"
# include "sysprof-document-dbus-message.h"
# include "sysprof-document-counter-value.h"
# include "sysprof-document-counter.h"
# include "sysprof-document-ctrdef.h"
# include "sysprof-document-ctrset.h"
# include "sysprof-document-exit.h"
# include "sysprof-document-file-chunk.h"
# include "sysprof-document-file.h"
# include "sysprof-document-fork.h"
# include "sysprof-document-frame.h"
# include "sysprof-document-jitmap.h"
# include "sysprof-document-loader.h"
# include "sysprof-document-log.h"
# include "sysprof-document-mark.h"
# include "sysprof-document-metadata.h"
# include "sysprof-document-mmap.h"
# include "sysprof-document-overlay.h"
# include "sysprof-document-process.h"
# include "sysprof-document-sample.h"
# include "sysprof-document-task.h"
# include "sysprof-document-traceable.h"
# include "sysprof-document.h"
# include "sysprof-elf-symbolizer.h"
# include "sysprof-energy-usage.h"
# include "sysprof-enums.h"
# include "sysprof-instrument.h"
# include "sysprof-jitmap-symbolizer.h"
# include "sysprof-kallsyms-symbolizer.h"
# include "sysprof-malloc-tracing.h"
# include "sysprof-mark-catalog.h"
# include "sysprof-memory-usage.h"
# include "sysprof-mount.h"
# include "sysprof-multi-symbolizer.h"
# include "sysprof-network-usage.h"
# include "sysprof-no-symbolizer.h"
# include "sysprof-power-profile.h"
# include "sysprof-profiler.h"
# include "sysprof-proxied-instrument.h"
# include "sysprof-recording.h"
# include "sysprof-sampler.h"
# include "sysprof-scheduler-details.h"
# include "sysprof-spawnable.h"
# include "sysprof-subprocess-output.h"
# include "sysprof-symbol.h"
# include "sysprof-symbolizer.h"
# include "sysprof-symbols-bundle.h"
# include "sysprof-system-logs.h"
# include "sysprof-thread-info.h"
# include "sysprof-time-span.h"
# include "sysprof-tracefd-consumer.h"
# include "sysprof-tracer.h"
# include "sysprof-user-sampler.h"
#undef SYSPROF_INSIDE

G_END_DECLS
