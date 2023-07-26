/* sysprof-storage-section.c
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

#include "sysprof-chart.h"
#include "sysprof-duplex-layer.h"
#include "sysprof-storage-section.h"
#include "sysprof-line-layer.h"
#include "sysprof-pair.h"
#include "sysprof-time-series.h"
#include "sysprof-time-span-layer.h"
#include "sysprof-xy-series.h"

struct _SysprofStorageSection
{
  SysprofSection parent_instance;
  GListStore *pairs;
};

G_DEFINE_FINAL_TYPE (SysprofStorageSection, sysprof_storage_section, SYSPROF_TYPE_SECTION)

enum {
  PROP_0,
  PROP_PAIRS,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
sysprof_storage_section_add_pair (SysprofStorageSection  *self,
                                  SysprofDocumentCounter *rx,
                                  SysprofDocumentCounter *tx)
{
  g_autoptr(SysprofPair) pair = NULL;

  g_assert (SYSPROF_IS_STORAGE_SECTION (self));
  g_assert (SYSPROF_IS_DOCUMENT_COUNTER (rx));
  g_assert (SYSPROF_IS_DOCUMENT_COUNTER (tx));

  pair = g_object_new (SYSPROF_TYPE_PAIR,
                       "first", rx,
                       "second", tx,
                       NULL);
  g_list_store_append (self->pairs, pair);
}

static void
sysprof_storage_section_session_set (SysprofSection *section,
                                     SysprofSession *session)
{
  SysprofStorageSection *self = (SysprofStorageSection *)section;
  g_autoptr(GListModel) counters = NULL;
  SysprofDocument *document;
  guint n_items;

  g_assert (SYSPROF_IS_STORAGE_SECTION (self));
  g_assert (!session || SYSPROF_IS_SESSION (session));

  if (session == NULL)
    {
      g_list_store_remove_all (self->pairs);
      return;
    }

  document = sysprof_session_get_document (session);
  counters = sysprof_document_list_counters (document);
  n_items = g_list_model_get_n_items (counters);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(SysprofDocumentCounter) counter = g_list_model_get_item (counters, i);
      const char *category = sysprof_document_counter_get_category (counter);
      const char *name = sysprof_document_counter_get_name (counter);

      if (g_strcmp0 (category, "Disk") == 0 && g_str_has_prefix (name, "Total Reads ("))
        {
          g_autoptr(SysprofDocumentCounter) writes = NULL;
          g_autofree char *alt_name = g_strdup_printf ("Total Writes %s", strchr (name, '('));

          if ((writes = sysprof_document_find_counter (document, "Disk", alt_name)))
            sysprof_storage_section_add_pair (self, counter, writes);
        }
    }
}

static void
sysprof_storage_section_dispose (GObject *object)
{
  SysprofStorageSection *self = (SysprofStorageSection *)object;

  gtk_widget_dispose_template (GTK_WIDGET (self), SYSPROF_TYPE_STORAGE_SECTION);

  g_clear_object (&self->pairs);

  G_OBJECT_CLASS (sysprof_storage_section_parent_class)->dispose (object);
}

static void
sysprof_storage_section_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  SysprofStorageSection *self = SYSPROF_STORAGE_SECTION (object);

  switch (prop_id)
    {
    case PROP_PAIRS:
      g_value_set_object (value, self->pairs);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_storage_section_class_init (SysprofStorageSectionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  SysprofSectionClass *section_class = SYSPROF_SECTION_CLASS (klass);

  object_class->dispose = sysprof_storage_section_dispose;
  object_class->get_property = sysprof_storage_section_get_property;

  section_class->session_set = sysprof_storage_section_session_set;

  properties [PROP_PAIRS] =
    g_param_spec_object ("pairs", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/sysprof/sysprof-storage-section.ui");

  g_type_ensure (SYSPROF_TYPE_CHART);
  g_type_ensure (SYSPROF_TYPE_DUPLEX_LAYER);
  g_type_ensure (SYSPROF_TYPE_PAIR);
}

static void
sysprof_storage_section_init (SysprofStorageSection *self)
{
  self->pairs = g_list_store_new (SYSPROF_TYPE_PAIR);

  gtk_widget_init_template (GTK_WIDGET (self));
}
