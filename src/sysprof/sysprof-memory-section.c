/* sysprof-memory-section.c
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

#include <sysprof.h>

#include "eggbitset.h"

#include "sysprof-document-bitset-index-private.h"
#include "sysprof-document-private.h"
#include "sysprof-leak-detector-private.h"

#include "sysprof-chart.h"
#include "sysprof-column-layer.h"
#include "sysprof-memory-callgraph-view.h"
#include "sysprof-memory-section.h"
#include "sysprof-sampled-model.h"
#include "sysprof-session-model-item.h"
#include "sysprof-session-model.h"
#include "sysprof-time-series.h"
#include "sysprof-time-span-layer.h"
#include "sysprof-value-axis.h"
#include "sysprof-xy-series.h"

struct _SysprofMemorySection
{
  SysprofSection parent_instance;

  SysprofMemoryCallgraphView *callgraph_view;
  GListModel *leaks;
};

G_DEFINE_FINAL_TYPE (SysprofMemorySection, sysprof_memory_section, SYSPROF_TYPE_SECTION)

enum {
  PROP_0,
  PROP_LEAKS,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static char *
format_number (gpointer unused,
               guint    number)
{
  if (number == 0)
    return NULL;
  return g_strdup_printf ("%'u", number);
}

static void
load_leaks_callgraph (GObject      *object,
                      GAsyncResult *result,
                      gpointer      user_data)
{
  SysprofMemorySection *self = (SysprofMemorySection *)object;
  g_autoptr(GListModel) leaks = NULL;

  g_assert (SYSPROF_IS_MEMORY_SECTION (self));
  g_assert (G_IS_TASK (result));

  if (!(leaks = g_task_propagate_pointer (G_TASK (result), NULL)))
    return;

  if (g_set_object (&self->leaks, leaks))
    g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_LEAKS]);
}

static void
sysprof_memory_section_leaks_worker (GTask        *task,
                                     gpointer      source_object,
                                     gpointer      task_data,
                                     GCancellable *cancellable)
{
  g_autoptr(GListModel) res = NULL;
  g_autoptr(EggBitset) allocs = NULL;
  g_autoptr(EggBitset) leaks = NULL;
  SysprofDocument *document = task_data;

  g_assert (G_IS_TASK (task));
  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  allocs = _sysprof_document_get_allocations (document);
  leaks = sysprof_leak_detector_detect (document, allocs);
  res = _sysprof_document_bitset_index_new (G_LIST_MODEL (document), leaks);

  g_task_return_pointer (task, g_steal_pointer (&res), g_object_unref);
}

static void
sysprof_memory_section_session_set (SysprofSection *section,
                                    SysprofSession *session)
{
  SysprofMemorySection *self = (SysprofMemorySection *)section;
  g_autoptr(GTask) task = NULL;
  SysprofDocument *document;

  g_assert (SYSPROF_IS_MEMORY_SECTION (self));
  g_assert (SYSPROF_IS_SESSION (session));

  document = sysprof_session_get_document (session);

  task = g_task_new (self, NULL, load_leaks_callgraph, NULL);
  g_task_set_task_data (task, g_object_ref (document), g_object_unref);
  g_task_run_in_thread (task, sysprof_memory_section_leaks_worker);
}

static void
sysprof_memory_section_dispose (GObject *object)
{
  SysprofMemorySection *self = (SysprofMemorySection *)object;

  gtk_widget_dispose_template (GTK_WIDGET (self), SYSPROF_TYPE_MEMORY_SECTION);

  G_OBJECT_CLASS (sysprof_memory_section_parent_class)->dispose (object);
}

static void
sysprof_memory_section_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  SysprofMemorySection *self = SYSPROF_MEMORY_SECTION (object);

  switch (prop_id)
    {
    case PROP_LEAKS:
      g_value_set_object (value, self->leaks);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_memory_section_class_init (SysprofMemorySectionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  SysprofSectionClass *section_class = SYSPROF_SECTION_CLASS (klass);

  object_class->dispose = sysprof_memory_section_dispose;
  object_class->get_property = sysprof_memory_section_get_property;

  section_class->session_set = sysprof_memory_section_session_set;

  properties [PROP_LEAKS] =
    g_param_spec_object ("leaks", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-memory-section.ui");
  gtk_widget_class_bind_template_child (widget_class, SysprofMemorySection, callgraph_view);
  gtk_widget_class_bind_template_callback (widget_class, format_number);

  g_type_ensure (SYSPROF_TYPE_CHART);
  g_type_ensure (SYSPROF_TYPE_COLUMN_LAYER);
  g_type_ensure (SYSPROF_TYPE_DOCUMENT_ALLOCATION);
  g_type_ensure (SYSPROF_TYPE_DOCUMENT_TRACEABLE);
  g_type_ensure (SYSPROF_TYPE_MEMORY_CALLGRAPH_VIEW);
  g_type_ensure (SYSPROF_TYPE_SAMPLED_MODEL);
  g_type_ensure (SYSPROF_TYPE_VALUE_AXIS);
  g_type_ensure (SYSPROF_TYPE_XY_SERIES);
}

static void
sysprof_memory_section_init (SysprofMemorySection *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}
