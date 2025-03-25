/* sysprof-session.c
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

#include <glib/gi18n.h>

#include "sysprof-mark-filter.h"
#include "sysprof-session-private.h"
#include "sysprof-value-axis.h"

struct _SysprofSession
{
  GObject          parent_instance;

  SysprofDocument *document;
  GtkEveryFilter  *filter;

  SysprofAxis     *visible_time_axis;
  SysprofAxis     *selected_time_axis;

  SysprofTimeSpan  document_time;
  SysprofTimeSpan  selected_time;
  SysprofTimeSpan  visible_time;

  guint            bottom_up : 1;
  guint            categorize_frames : 1;
  guint            hide_system_libraries : 1;
  guint            ignore_process_0 : 1;
  guint            ignore_kernel_processes : 1;
  guint            include_threads : 1;
  guint            left_heavy : 1;
  guint            merge_similar_processes : 1;
};

enum {
  PROP_0,
  PROP_BOTTOM_UP,
  PROP_CATEGORIZE_FRAMES,
  PROP_DOCUMENT,
  PROP_DOCUMENT_TIME,
  PROP_FILTER,
  PROP_HIDE_SYSTEM_LIBRARIES,
  PROP_IGNORE_KERNEL_PROCESSES,
  PROP_IGNORE_PROCESS_0,
  PROP_INCLUDE_THREADS,
  PROP_LEFT_HEAVY,
  PROP_MERGE_SIMILAR_PROCESSES,
  PROP_SELECTED_TIME,
  PROP_SELECTED_TIME_AXIS,
  PROP_VISIBLE_TIME,
  PROP_VISIBLE_TIME_AXIS,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofSession, sysprof_session, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static void
sysprof_session_update_axis (SysprofSession *self)
{
  g_assert (SYSPROF_IS_SESSION (self));

  sysprof_value_axis_set_min_value (SYSPROF_VALUE_AXIS (self->visible_time_axis),
                                    self->visible_time.begin_nsec);
  sysprof_value_axis_set_max_value (SYSPROF_VALUE_AXIS (self->visible_time_axis),
                                    self->visible_time.end_nsec);

  sysprof_value_axis_set_min_value (SYSPROF_VALUE_AXIS (self->selected_time_axis),
                                    self->selected_time.begin_nsec);
  sysprof_value_axis_set_max_value (SYSPROF_VALUE_AXIS (self->selected_time_axis),
                                    self->selected_time.end_nsec);
}

static void
sysprof_session_set_document (SysprofSession  *self,
                              SysprofDocument *document)
{
  const SysprofTimeSpan *time_span;

  g_assert (SYSPROF_IS_SESSION (self));
  g_assert (!document || SYSPROF_IS_DOCUMENT (document));

  if (!g_set_object (&self->document, document))
    return;

  /* Select/show the entire document time span */
  time_span = sysprof_document_get_time_span (document);
  self->selected_time = self->visible_time = self->document_time = *time_span;
  sysprof_session_update_axis (self);
}

static void
sysprof_session_dispose (GObject *object)
{
  SysprofSession *self = (SysprofSession *)object;

  g_clear_object (&self->visible_time_axis);
  g_clear_object (&self->selected_time_axis);
  g_clear_object (&self->document);
  g_clear_object (&self->filter);

  G_OBJECT_CLASS (sysprof_session_parent_class)->dispose (object);
}

static void
sysprof_session_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  SysprofSession *self = SYSPROF_SESSION (object);

  switch (prop_id)
    {
    case PROP_BOTTOM_UP:
      g_value_set_boolean (value, self->bottom_up);
      break;

    case PROP_CATEGORIZE_FRAMES:
      g_value_set_boolean (value, self->categorize_frames);
      break;

    case PROP_DOCUMENT:
      g_value_set_object (value, sysprof_session_get_document (self));
      break;

    case PROP_DOCUMENT_TIME:
      g_value_set_boxed (value, sysprof_session_get_document_time (self));
      break;

    case PROP_FILTER:
      g_value_set_object (value, sysprof_session_get_filter (self));
      break;

    case PROP_HIDE_SYSTEM_LIBRARIES:
      g_value_set_boolean (value, self->hide_system_libraries);
      break;

    case PROP_IGNORE_PROCESS_0:
      g_value_set_boolean (value, self->ignore_process_0);
      break;

    case PROP_IGNORE_KERNEL_PROCESSES:
      g_value_set_boolean (value, self->ignore_kernel_processes);
      break;

    case PROP_INCLUDE_THREADS:
      g_value_set_boolean (value, self->include_threads);
      break;

    case PROP_LEFT_HEAVY:
      g_value_set_boolean (value, self->left_heavy);
      break;

    case PROP_MERGE_SIMILAR_PROCESSES:
      g_value_set_boolean (value, self->merge_similar_processes);
      break;

    case PROP_SELECTED_TIME:
      g_value_set_boxed (value, sysprof_session_get_selected_time (self));
      break;

    case PROP_VISIBLE_TIME:
      g_value_set_boxed (value, sysprof_session_get_visible_time (self));
      break;

    case PROP_SELECTED_TIME_AXIS:
      g_value_set_object (value, sysprof_session_get_selected_time_axis (self));
      break;

    case PROP_VISIBLE_TIME_AXIS:
      g_value_set_object (value, sysprof_session_get_visible_time_axis (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_session_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  SysprofSession *self = SYSPROF_SESSION (object);

  switch (prop_id)
    {
    case PROP_BOTTOM_UP:
      self->bottom_up = g_value_get_boolean (value);
      break;

    case PROP_CATEGORIZE_FRAMES:
      self->categorize_frames = g_value_get_boolean (value);
      break;

    case PROP_DOCUMENT:
      sysprof_session_set_document (self, g_value_get_object (value));
      break;

    case PROP_HIDE_SYSTEM_LIBRARIES:
      self->hide_system_libraries = g_value_get_boolean (value);
      break;

    case PROP_IGNORE_PROCESS_0:
      self->ignore_process_0 = g_value_get_boolean (value);
      break;

    case PROP_IGNORE_KERNEL_PROCESSES:
      self->ignore_kernel_processes = g_value_get_boolean (value);
      break;

    case PROP_INCLUDE_THREADS:
      self->include_threads = g_value_get_boolean (value);
      break;

    case PROP_LEFT_HEAVY:
      self->left_heavy = g_value_get_boolean (value);
      break;

    case PROP_MERGE_SIMILAR_PROCESSES:
      self->merge_similar_processes = g_value_get_boolean (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_session_class_init (SysprofSessionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = sysprof_session_dispose;
  object_class->get_property = sysprof_session_get_property;
  object_class->set_property = sysprof_session_set_property;

  properties [PROP_BOTTOM_UP] =
    g_param_spec_boolean ("bottom-up", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_CATEGORIZE_FRAMES] =
    g_param_spec_boolean ("categorize-frames", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_DOCUMENT] =
    g_param_spec_object ("document", NULL, NULL,
                         SYSPROF_TYPE_DOCUMENT,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_DOCUMENT_TIME] =
    g_param_spec_boxed ("document-time", NULL, NULL,
                        SYSPROF_TYPE_TIME_SPAN,
                        (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_FILTER] =
    g_param_spec_object ("filter", NULL, NULL,
                         GTK_TYPE_FILTER,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_HIDE_SYSTEM_LIBRARIES] =
    g_param_spec_boolean ("hide-system-libraries", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_IGNORE_PROCESS_0] =
    g_param_spec_boolean ("ignore-process-0", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_IGNORE_KERNEL_PROCESSES] =
    g_param_spec_boolean ("ignore-kernel-processes", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_INCLUDE_THREADS] =
    g_param_spec_boolean ("include-threads", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_LEFT_HEAVY] =
    g_param_spec_boolean ("left-heavy", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_MERGE_SIMILAR_PROCESSES] =
    g_param_spec_boolean ("merge-similar-processes", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SELECTED_TIME] =
    g_param_spec_boxed ("selected-time", NULL, NULL,
                        SYSPROF_TYPE_TIME_SPAN,
                        (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_VISIBLE_TIME] =
    g_param_spec_boxed ("visible-time", NULL, NULL,
                        SYSPROF_TYPE_TIME_SPAN,
                        (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SELECTED_TIME_AXIS] =
    g_param_spec_object ("selected-time-axis", NULL, NULL,
                         SYSPROF_TYPE_AXIS,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_VISIBLE_TIME_AXIS] =
    g_param_spec_object ("visible-time-axis", NULL, NULL,
                         SYSPROF_TYPE_AXIS,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_session_init (SysprofSession *self)
{
  self->filter = gtk_every_filter_new ();
  self->selected_time_axis = sysprof_value_axis_new (0, 0);
  self->visible_time_axis = sysprof_value_axis_new (0, 0);

  self->categorize_frames = TRUE;
}

SysprofSession *
sysprof_session_new (SysprofDocument *document)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (document), NULL);

  return g_object_new (SYSPROF_TYPE_SESSION,
                       "document", document,
                       NULL);
}

/**
 * sysprof_session_get_document:
 * @self: a #SysprofSession
 *
 * Returns: (transfer none): a #SysprofDocument
 */
SysprofDocument *
sysprof_session_get_document (SysprofSession *self)
{
  g_return_val_if_fail (SYSPROF_IS_SESSION (self), NULL);

  return self->document;
}

/**
 * sysprof_session_get_filter:
 * @self: a #SysprofSession
 *
 * Gets the filter for the session which can remove #SysprofDocumentFrame
 * which are not matching the current selection filters.
 *
 * Returns: (transfer none) (nullable): a #GtkFilter
 */
GtkFilter *
sysprof_session_get_filter (SysprofSession *self)
{
  g_return_val_if_fail (SYSPROF_IS_SESSION (self), NULL);

  return GTK_FILTER (self->filter);
}

const SysprofTimeSpan *
sysprof_session_get_selected_time (SysprofSession *self)
{
  g_return_val_if_fail (SYSPROF_IS_SESSION (self), NULL);

  return &self->selected_time;
}

const SysprofTimeSpan *
sysprof_session_get_visible_time (SysprofSession *self)
{
  g_return_val_if_fail (SYSPROF_IS_SESSION (self), NULL);

  return &self->visible_time;
}

const SysprofTimeSpan *
sysprof_session_get_document_time (SysprofSession *self)
{
  g_return_val_if_fail (SYSPROF_IS_SESSION (self), NULL);

  return &self->document_time;
}

void
sysprof_session_select_time (SysprofSession        *self,
                             const SysprofTimeSpan *time_span)
{
  SysprofTimeSpan document_time_span;
  gboolean emit_for_visible = FALSE;

  g_return_if_fail (SYSPROF_IS_SESSION (self));

  document_time_span = *sysprof_document_get_time_span (self->document);

  if (time_span == NULL)
    time_span = &document_time_span;

  self->selected_time = sysprof_time_span_order (*time_span);
  sysprof_time_span_clamp (&self->selected_time, document_time_span);

  if (self->visible_time.begin_nsec > time_span->begin_nsec)
    {
      self->visible_time.begin_nsec = time_span->begin_nsec;
      emit_for_visible = TRUE;
    }

  if (self->visible_time.end_nsec < time_span->end_nsec)
    {
      self->visible_time.end_nsec = time_span->end_nsec;
      emit_for_visible = TRUE;
    }

  sysprof_session_update_axis (self);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SELECTED_TIME]);

  if (emit_for_visible)
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VISIBLE_TIME]);
}

/**
 * sysprof_session_get_selected_time_axis:
 * @self: a #SysprofSession
 *
 * Returns: (transfer none) (nullable): a #SysprofAxis
 */
SysprofAxis *
sysprof_session_get_selected_time_axis (SysprofSession *self)
{
  g_return_val_if_fail (SYSPROF_IS_SESSION (self), NULL);

  return self->selected_time_axis;
}

/**
 * sysprof_session_get_visible_time_axis:
 * @self: a #SysprofSession
 *
 * Returns: (transfer none) (nullable): a #SysprofAxis
 */
SysprofAxis *
sysprof_session_get_visible_time_axis (SysprofSession *self)
{
  g_return_val_if_fail (SYSPROF_IS_SESSION (self), NULL);

  return self->visible_time_axis;
}

void
sysprof_session_zoom_to_selection (SysprofSession *self)
{
  g_return_if_fail (SYSPROF_IS_SESSION (self));

  if (memcmp (&self->visible_time, &self->selected_time, sizeof self->visible_time) == 0)
    return;

  self->visible_time = self->selected_time;

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VISIBLE_TIME]);

  sysprof_session_update_axis (self);
}

void
sysprof_session_filter_by_mark (SysprofSession     *self,
                                SysprofMarkCatalog *catalog)
{
  g_return_if_fail (SYSPROF_IS_SESSION (self));
  g_return_if_fail (catalog == NULL || SYSPROF_IS_MARK_CATALOG (catalog));

  while (g_list_model_get_n_items (G_LIST_MODEL (self->filter)) > 0)
    gtk_multi_filter_remove (GTK_MULTI_FILTER (self->filter), 0);

  if (catalog)
    {
      g_autoptr(SysprofMarkFilter) mark_filter = sysprof_mark_filter_new (self->document, catalog);

      gtk_multi_filter_append (GTK_MULTI_FILTER (self->filter), GTK_FILTER (g_steal_pointer (&mark_filter)));
    }
}

static char *
get_time_str (gint64 o)
{
  char str[32];

  if (o == 0)
    g_snprintf (str, sizeof str, "%.3lf s", .0);
  else if (o < 1000000)
    g_snprintf (str, sizeof str, "%.3lf μs", o/1000.);
  else if (o < SYSPROF_NSEC_PER_SEC)
    g_snprintf (str, sizeof str, "%.3lf ms", o/1000000.);
  else
    g_snprintf (str, sizeof str, "%.3lf s", o/(double)SYSPROF_NSEC_PER_SEC);

  return g_strdup (str);
}

static void
append_time_string (GString               *str,
                    const SysprofTimeSpan *span)
{
  g_autofree char *begin_str = NULL;

  g_assert (str != NULL);
  g_assert (span != NULL);

  begin_str = get_time_str (span->begin_nsec);

  g_string_append (str, begin_str);

  if (span->begin_nsec != span->end_nsec)
    {
      g_autofree char *end_str = get_time_str (span->end_nsec - span->begin_nsec);

      g_string_append_printf (str, " (%s)", end_str);
    }
}

char *
_sysprof_session_describe (SysprofSession *self,
                           gpointer        item)
{
  g_autofree char *text = NULL;

  g_return_val_if_fail (SYSPROF_IS_SESSION (self), NULL);

  if (self->document == NULL)
    return NULL;

  if (SYSPROF_IS_DOCUMENT_MARK (item))
    {
      SysprofDocumentMark *mark = item;
      const SysprofTimeSpan *begin = sysprof_document_get_time_span (self->document);
      GString *str = g_string_new (NULL);
      const char *group = sysprof_document_mark_get_group (mark);
      const char *name = sysprof_document_mark_get_name (mark);
      const char *message = sysprof_document_mark_get_message (mark);
      SysprofTimeSpan span = {
        .begin_nsec = sysprof_document_frame_get_time (item),
        .end_nsec = sysprof_document_mark_get_end_time (mark),
      };

      span = sysprof_time_span_relative_to (span, begin->begin_nsec);

      append_time_string (str, &span);
      g_string_append_printf (str, ": %s / %s", group, name);

      if (message && message[0])
        g_string_append_printf (str, ": %s", message);

      return g_string_free (str, FALSE);
    }

  if (SYSPROF_IS_DOCUMENT_COUNTER_VALUE (item))
    {
      SysprofDocumentCounterValue *value = item;
      g_autofree char *value_str = sysprof_document_counter_value_format (value);
      const SysprofTimeSpan *begin = sysprof_document_get_time_span (self->document);
      gint64 t = sysprof_document_counter_value_get_time (value);
      GString *str = g_string_new (NULL);
      SysprofTimeSpan span = { t, t };

      span = sysprof_time_span_relative_to (span, begin->begin_nsec);

      append_time_string (str, &span);
      g_string_append_printf (str, ": %s", value_str);

      return g_string_free (str, FALSE);
    }

  if (SYSPROF_IS_DOCUMENT_LOG (item))
    {
      SysprofDocumentLog *log = item;
      const SysprofTimeSpan *begin = sysprof_document_get_time_span (self->document);
      GString *str = g_string_new (NULL);
      const char *domain = sysprof_document_log_get_domain (log);
      const char *message = sysprof_document_log_get_message (log);
      gint64 t = sysprof_document_frame_get_time (item);
      SysprofTimeSpan span = { t, t };

      span = sysprof_time_span_relative_to (span, begin->begin_nsec);

      append_time_string (str, &span);
      g_string_append_printf (str, ": %s: %s", domain, message);

      return g_string_free (str, FALSE);
    }

  if (SYSPROF_IS_DOCUMENT_PROCESS (item))
    {
      SysprofDocumentProcess *process = item;
      const SysprofTimeSpan *begin = sysprof_document_get_time_span (self->document);
      g_autofree char *title = sysprof_document_process_dup_title (process);
      GString *str = g_string_new (NULL);
      gint64 t = sysprof_document_frame_get_time (item);
      SysprofTimeSpan span = { t, sysprof_document_process_get_exit_time (process) };

      span = sysprof_time_span_relative_to (span, begin->begin_nsec);

      if (span.begin_nsec < 0)
        span.begin_nsec = 0;

      append_time_string (str, &span);
      g_string_append_printf (str, ": %s", title);

      return g_string_free (str, FALSE);
    }

  return NULL;
}
