/* sysprof-document.c
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

#include <fcntl.h>

#include <glib/gstdio.h>
#include <glib/gi18n.h>

#include <libdex.h>

#include "timsort/gtktimsortprivate.h"

#include "sysprof-document-private.h"

#include "sysprof-bundled-symbolizer-private.h"
#include "sysprof-callgraph-private.h"
#include "sysprof-cpu-info-private.h"
#include "sysprof-document-allocation.h"
#include "sysprof-document-bitset-index-private.h"
#include "sysprof-document-counter-private.h"
#include "sysprof-document-ctrdef.h"
#include "sysprof-document-ctrset.h"
#include "sysprof-document-file-chunk.h"
#include "sysprof-document-file-private.h"
#include "sysprof-document-frame-private.h"
#include "sysprof-document-jitmap.h"
#include "sysprof-document-mmap.h"
#include "sysprof-document-overlay.h"
#include "sysprof-document-process-private.h"
#include "sysprof-document-sample.h"
#include "sysprof-document-symbols-private.h"
#include "sysprof-mark-catalog-private.h"
#include "sysprof-mount-private.h"
#include "sysprof-mount-device-private.h"
#include "sysprof-mount-namespace-private.h"
#include "sysprof-process-info-private.h"
#include "sysprof-strings-private.h"
#include "sysprof-symbol-private.h"
#include "sysprof-symbolizer-private.h"

#include "line-reader-private.h"

#define MAX_STACK_DEPTH 128

struct _SysprofDocument
{
  GObject                   parent_instance;

  SysprofTimeSpan           time_span;

  char                     *title;

  GArray                   *frames;
  GMappedFile              *mapped_file;
  const guint8             *base;

  GListStore               *cpu_info;

  GListStore               *counters;
  GHashTable               *counter_id_to_values;

  SysprofStrings           *strings;
  SysprofSymbol            *missing_process;

  EggBitset                *allocations;
  EggBitset                *ctrdefs;
  EggBitset                *ctrsets;
  EggBitset                *dbus_messages;
  EggBitset                *file_chunks;
  EggBitset                *jitmaps;
  EggBitset                *logs;
  EggBitset                *marks;
  EggBitset                *metadata;
  EggBitset                *mmaps;
  EggBitset                *overlays;
  EggBitset                *pids;
  EggBitset                *processes;
  EggBitset                *samples;
  EggBitset                *samples_with_context_switch;
  EggBitset                *traceables;

  GHashTable               *files_first_position;
  GHashTable               *pid_to_process_info;
  GHashTable               *tid_to_symbol;
  GHashTable               *mark_groups;

  SysprofMountNamespace    *mount_namespace;

  SysprofDocumentSymbols   *symbols;

  guint                     busy_count;

  SysprofCaptureFileHeader  header;
  guint                     needs_swap : 1;
};

enum {
  PROP_0,
  PROP_ALLOCATIONS,
  PROP_BUSY,
  PROP_COUNTERS,
  PROP_CPU_INFO,
  PROP_DBUS_MESSAGES,
  PROP_FILES,
  PROP_LOGS,
  PROP_MARKS,
  PROP_MARKS_CATALOG,
  PROP_METADATA,
  PROP_PROCESSES,
  PROP_SAMPLES,
  PROP_TIME_SPAN,
  PROP_TITLE,
  PROP_SUBTITLE,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static inline guint16
swap_uint16 (gboolean needs_swap,
             guint16  value)
{
  if (!needs_swap)
    return value;

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  return GUINT16_FROM_BE (value);
#else
  return GUINT16_FROM_LE (value);
#endif
}

static inline guint32
swap_uint32 (gboolean needs_swap,
             guint32  value)
{
  if (!needs_swap)
    return value;

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  return GUINT32_FROM_BE (value);
#else
  return GUINT32_FROM_LE (value);
#endif
}

static inline int
swap_int32 (gboolean needs_swap,
            int      value)
{
  if (!needs_swap)
    return value;

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  return GINT32_FROM_BE (value);
#else
  return GINT32_FROM_LE (value);
#endif
}

static inline gint64
swap_int64 (gboolean needs_swap,
            gint64   value)
{
  if G_LIKELY (!needs_swap)
    return value;

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  return GINT64_FROM_BE (value);
#else
  return GINT64_FROM_LE (value);
#endif
}

static inline guint64
swap_uint64 (gboolean needs_swap,
             guint64  value)
{
  if G_LIKELY (!needs_swap)
    return value;

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  return GUINT64_FROM_BE (value);
#else
  return GUINT64_FROM_LE (value);
#endif
}

static GType
sysprof_document_get_item_type (GListModel *model)
{
  return SYSPROF_TYPE_DOCUMENT_FRAME;
}

static guint
sysprof_document_get_n_items (GListModel *model)
{
  return SYSPROF_DOCUMENT (model)->frames->len;
}

static gpointer
sysprof_document_get_item (GListModel *model,
                           guint       position)
{
  SysprofDocument *self = SYSPROF_DOCUMENT (model);
  SysprofDocumentFramePointer *ptr;
  SysprofDocumentFrame *ret;

  if (position >= self->frames->len)
    return NULL;

  ptr = &g_array_index (self->frames, SysprofDocumentFramePointer, position);
  ret = _sysprof_document_frame_new (self->mapped_file,
                                     (gconstpointer)&self->base[ptr->offset],
                                     ptr->length,
                                     self->needs_swap,
                                     self->header.time,
                                     self->header.end_time);

  /* Annotate processes with pre-calculated info */
  if (SYSPROF_IS_DOCUMENT_PROCESS (ret))
    {
      int pid = sysprof_document_frame_get_pid (ret);
      SysprofProcessInfo *process_info = g_hash_table_lookup (self->pid_to_process_info, GINT_TO_POINTER (pid));

      if (process_info != NULL)
        _sysprof_document_process_set_info (SYSPROF_DOCUMENT_PROCESS (ret), process_info);
    }

  return ret;
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = sysprof_document_get_item_type;
  iface->get_n_items = sysprof_document_get_n_items;
  iface->get_item = sysprof_document_get_item;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (SysprofDocument, sysprof_document, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

static void
sysprof_document_mark_busy (SysprofDocument *self)
{
  g_assert (SYSPROF_IS_DOCUMENT (self));

  if (++self->busy_count == 1)
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BUSY]);
}

static void
sysprof_document_unmark_busy (SysprofDocument *self)
{
  g_assert (SYSPROF_IS_DOCUMENT (self));

  if (--self->busy_count == 0)
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BUSY]);
}

static void
sysprof_document_mark_busy_for_task (SysprofDocument *self,
                                     GTask           *task)
{
  g_assert (SYSPROF_IS_DOCUMENT (self));
  g_assert (G_IS_TASK (task));

  g_signal_connect_object (task,
                           "notify::completed",
                           G_CALLBACK (sysprof_document_unmark_busy),
                           self,
                           G_CONNECT_SWAPPED);

  sysprof_document_mark_busy (self);
}


EggBitset *
_sysprof_document_traceables (SysprofDocument *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  return self->traceables;
}

SysprofProcessInfo *
_sysprof_document_process_info (SysprofDocument *self,
                                int              pid,
                                gboolean         may_create)
{
  SysprofProcessInfo *process_info;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  if (pid < 0)
    return NULL;

  process_info = g_hash_table_lookup (self->pid_to_process_info, GINT_TO_POINTER (pid));

  if (process_info == NULL && may_create)
    {
      process_info = sysprof_process_info_new (sysprof_mount_namespace_copy (self->mount_namespace), pid);
      g_hash_table_insert (self->pid_to_process_info, GINT_TO_POINTER (pid), process_info);
    }

  return process_info;
}

static void
decode_space (gchar **str)
{
  /* Replace encoded space "\040" with ' ' */
  if (strstr (*str, "\\040"))
    {
      g_auto(GStrv) parts = g_strsplit (*str, "\\040", 0);
      g_free (*str);
      *str = g_strjoinv (" ", parts);
    }
}

static inline gboolean
has_null_byte (const char *str,
               const char *endptr)
{
  for (const char *c = str; c < endptr; c++)
    {
      if (*c == '\0')
        return TRUE;
    }

  return FALSE;
}

static void
sysprof_document_finalize (GObject *object)
{
  SysprofDocument *self = (SysprofDocument *)object;

  g_clear_pointer (&self->strings, sysprof_strings_unref);

  g_clear_pointer (&self->title, g_free);

  g_clear_pointer (&self->pid_to_process_info, g_hash_table_unref);
  g_clear_pointer (&self->tid_to_symbol, g_hash_table_unref);
  g_clear_pointer (&self->mapped_file, g_mapped_file_unref);
  g_clear_pointer (&self->frames, g_array_unref);

  g_clear_pointer (&self->allocations, egg_bitset_unref);
  g_clear_pointer (&self->ctrdefs, egg_bitset_unref);
  g_clear_pointer (&self->ctrsets, egg_bitset_unref);
  g_clear_pointer (&self->dbus_messages, egg_bitset_unref);
  g_clear_pointer (&self->marks, egg_bitset_unref);
  g_clear_pointer (&self->metadata, egg_bitset_unref);
  g_clear_pointer (&self->file_chunks, egg_bitset_unref);
  g_clear_pointer (&self->jitmaps, egg_bitset_unref);
  g_clear_pointer (&self->logs, egg_bitset_unref);
  g_clear_pointer (&self->mmaps, egg_bitset_unref);
  g_clear_pointer (&self->overlays, egg_bitset_unref);
  g_clear_pointer (&self->pids, egg_bitset_unref);
  g_clear_pointer (&self->processes, egg_bitset_unref);
  g_clear_pointer (&self->samples, egg_bitset_unref);
  g_clear_pointer (&self->samples_with_context_switch, egg_bitset_unref);
  g_clear_pointer (&self->traceables, egg_bitset_unref);

  g_clear_pointer (&self->mark_groups, g_hash_table_unref);

  g_clear_object (&self->counters);
  g_clear_pointer (&self->counter_id_to_values, g_hash_table_unref);

  g_clear_object (&self->cpu_info);

  g_clear_object (&self->mount_namespace);
  g_clear_object (&self->symbols);

  g_clear_pointer (&self->files_first_position, g_hash_table_unref);

  G_OBJECT_CLASS (sysprof_document_parent_class)->finalize (object);
}

static void
sysprof_document_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  SysprofDocument *self = SYSPROF_DOCUMENT (object);

  switch (prop_id)
    {
    case PROP_ALLOCATIONS:
      g_value_take_object (value, sysprof_document_list_allocations (self));
      break;

    case PROP_BUSY:
      g_value_set_boolean (value, sysprof_document_get_busy (self));
      break;

    case PROP_COUNTERS:
      g_value_take_object (value, sysprof_document_list_counters (self));
      break;

    case PROP_CPU_INFO:
      g_value_take_object (value, sysprof_document_list_cpu_info (self));
      break;

    case PROP_DBUS_MESSAGES:
      g_value_take_object (value, sysprof_document_list_dbus_messages (self));
      break;

    case PROP_FILES:
      g_value_take_object (value, sysprof_document_list_files (self));
      break;

    case PROP_LOGS:
      g_value_take_object (value, sysprof_document_list_logs (self));
      break;

    case PROP_MARKS:
      g_value_take_object (value, sysprof_document_list_marks (self));
      break;

    case PROP_MARKS_CATALOG:
      g_value_take_object (value, sysprof_document_catalog_marks (self));
      break;

    case PROP_METADATA:
      g_value_take_object (value, sysprof_document_list_metadata (self));
      break;

    case PROP_PROCESSES:
      g_value_take_object (value, sysprof_document_list_processes (self));
      break;

    case PROP_SAMPLES:
      g_value_take_object (value, sysprof_document_list_samples (self));
      break;

    case PROP_TIME_SPAN:
      g_value_set_boxed (value, sysprof_document_get_time_span (self));
      break;

    case PROP_TITLE:
      g_value_take_string (value, sysprof_document_dup_title (self));
      break;

    case PROP_SUBTITLE:
      g_value_take_string (value, sysprof_document_dup_subtitle (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_document_class_init (SysprofDocumentClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_document_finalize;
  object_class->get_property = sysprof_document_get_property;

  properties [PROP_ALLOCATIONS] =
    g_param_spec_object ("allocations", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_BUSY] =
    g_param_spec_boolean ("busy", NULL, NULL,
                         FALSE,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_COUNTERS] =
    g_param_spec_object ("counters", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_CPU_INFO] =
    g_param_spec_object ("cpu-info", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_DBUS_MESSAGES] =
    g_param_spec_object ("dbus-messages", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_FILES] =
    g_param_spec_object ("files", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_LOGS] =
    g_param_spec_object ("logs", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_MARKS] =
    g_param_spec_object ("marks", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_MARKS_CATALOG] =
    g_param_spec_object ("marks-catalog", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_METADATA] =
    g_param_spec_object ("metadata", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_PROCESSES] =
    g_param_spec_object ("processes", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SAMPLES] =
    g_param_spec_object ("samples", NULL, NULL,
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_TIME_SPAN] =
    g_param_spec_boxed ("time-span", NULL, NULL,
                        SYSPROF_TYPE_TIME_SPAN,
                        (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_TITLE] =
    g_param_spec_string ("title", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SUBTITLE] =
    g_param_spec_string ("subtitle", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_document_init (SysprofDocument *self)
{
  self->strings = sysprof_strings_new ();

  self->frames = g_array_new (FALSE, FALSE, sizeof (SysprofDocumentFramePointer));

  self->cpu_info = g_list_store_new (SYSPROF_TYPE_CPU_INFO);

  self->missing_process = _sysprof_symbol_new (g_ref_string_new (_("Unknown Process")),
                                               NULL, NULL, 0, 0, SYSPROF_SYMBOL_KIND_UNWINDABLE);

  self->counters = g_list_store_new (SYSPROF_TYPE_DOCUMENT_COUNTER);
  self->counter_id_to_values = g_hash_table_new_full (NULL, NULL, NULL,
                                                      (GDestroyNotify)g_array_unref);

  self->allocations = egg_bitset_new_empty ();
  self->ctrdefs = egg_bitset_new_empty ();
  self->ctrsets = egg_bitset_new_empty ();
  self->dbus_messages = egg_bitset_new_empty ();
  self->marks = egg_bitset_new_empty ();
  self->metadata = egg_bitset_new_empty ();
  self->file_chunks = egg_bitset_new_empty ();
  self->jitmaps = egg_bitset_new_empty ();
  self->logs = egg_bitset_new_empty ();
  self->mmaps = egg_bitset_new_empty ();
  self->overlays = egg_bitset_new_empty ();
  self->pids = egg_bitset_new_empty ();
  self->processes = egg_bitset_new_empty ();
  self->samples = egg_bitset_new_empty ();
  self->samples_with_context_switch = egg_bitset_new_empty ();
  self->traceables = egg_bitset_new_empty ();

  self->files_first_position = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  self->pid_to_process_info = g_hash_table_new_full (NULL, NULL, NULL, (GDestroyNotify)sysprof_process_info_unref);
  self->tid_to_symbol = g_hash_table_new_full (NULL, NULL, NULL, (GDestroyNotify)g_object_unref);
  self->mark_groups = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_hash_table_unref);

  self->mount_namespace = sysprof_mount_namespace_new ();
}

static void
sysprof_document_load_memory_maps (SysprofDocument *self)
{
  EggBitsetIter iter;
  guint i;

  g_assert (SYSPROF_IS_DOCUMENT (self));

  if (egg_bitset_iter_init_first (&iter, self->mmaps, &i))
    {
      do
        {
          g_autoptr(SysprofDocumentMmap) map = sysprof_document_get_item ((GListModel *)self, i);
          int pid = sysprof_document_frame_get_pid (SYSPROF_DOCUMENT_FRAME (map));
          SysprofProcessInfo *process_info = _sysprof_document_process_info (self, pid, TRUE);

          sysprof_address_layout_take (process_info->address_layout, g_steal_pointer (&map));
        }
      while (egg_bitset_iter_next (&iter, &i));
    }
}

static void
sysprof_document_load_mounts (SysprofDocument *self)
{
  g_autoptr(SysprofDocumentFile) file = NULL;
  g_autoptr(GBytes) bytes = NULL;
  LineReader reader;
  GHashTableIter iter;
  const char *contents;
  gsize contents_len;
  gsize line_len;
  char *line;

  g_assert (SYSPROF_IS_DOCUMENT (self));

  if (!(file = sysprof_document_lookup_file (self, "/proc/mounts")) ||
      !(bytes = sysprof_document_file_dup_bytes (file)))
    return;

  contents = g_bytes_get_data (bytes, &contents_len);

  g_assert (contents != NULL);
  g_assert (contents[contents_len] == 0);

  line_reader_init (&reader, (char *)contents, contents_len);
  while ((line = line_reader_next (&reader, &line_len)))
    {
      g_autoptr(SysprofMountDevice) mount_device = NULL;
      SysprofProcessInfo *process_info = NULL;
      g_autofree char *subvol = NULL;
      g_auto(GStrv) parts = NULL;
      const char *filesystem;
      const char *mountpoint;
      const char *device;
      const char *options;

      line[line_len] = 0;

      parts = g_strsplit (line, " ", 5);

      /* Field 0: device
       * Field 1: mountpoint
       * Field 2: filesystem
       * Field 3: Options
       * .. Ignored ..
       */
      if (g_strv_length (parts) != 5)
        continue;

      for (guint j = 0; parts[j]; j++)
        decode_space (&parts[j]);

      device = parts[0];
      mountpoint = parts[1];
      filesystem = parts[2];
      options = parts[3];

      if (g_strcmp0 (filesystem, "btrfs") == 0)
        {
          g_auto(GStrv) opts = g_strsplit (options, ",", 0);

          for (guint k = 0; opts[k]; k++)
            {
              if (g_str_has_prefix (opts[k], "subvol="))
                {
                  subvol = g_strdup (opts[k] + strlen ("subvol="));
                  break;
                }
            }
        }

      mount_device =
        sysprof_mount_device_new (sysprof_strings_get (self->strings, device),
                                  sysprof_strings_get (self->strings, mountpoint),
                                  sysprof_strings_get (self->strings, subvol));

      g_hash_table_iter_init (&iter, self->pid_to_process_info);
      while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&process_info))
        sysprof_mount_namespace_add_device (process_info->mount_namespace,
                                            g_object_ref (mount_device));

      sysprof_mount_namespace_add_device (self->mount_namespace, g_object_ref (mount_device));
    }
}

static void
sysprof_document_load_mountinfo (SysprofDocument *self,
                                 int              pid,
                                 GBytes          *bytes)
{
  SysprofProcessInfo *process_info;
  const char *contents;
  LineReader reader;
  gsize contents_len;
  gsize line_len;
  char *line;

  g_assert (SYSPROF_IS_DOCUMENT (self));
  g_assert (bytes != NULL);

  contents = g_bytes_get_data (bytes, &contents_len);

  g_assert (contents != NULL);
  g_assert (contents[contents_len] == 0);

  process_info = _sysprof_document_process_info (self, pid, TRUE);

  g_assert (process_info != NULL);
  g_assert (process_info->mount_namespace != NULL);

  line_reader_init (&reader, (char *)contents, contents_len);
  while ((line = line_reader_next (&reader, &line_len)))
    {
      g_autoptr(SysprofMount) mount = NULL;

      line[line_len] = 0;

      if ((mount = _sysprof_mount_new_for_mountinfo (self->strings, line)))
        sysprof_mount_namespace_add_mount (process_info->mount_namespace, g_steal_pointer (&mount));
    }
}

static void
sysprof_document_load_mountinfos (SysprofDocument *self)
{
  EggBitsetIter iter;
  guint pid;

  g_assert (SYSPROF_IS_DOCUMENT (self));

  if (egg_bitset_iter_init_first (&iter, self->pids, &pid))
    {
      do
        {
          g_autofree char *path = g_strdup_printf ("/proc/%u/mountinfo", pid);
          g_autoptr(SysprofDocumentFile) file = sysprof_document_lookup_file (self, path);

          if (file != NULL)
            {
              g_autoptr(GBytes) bytes = sysprof_document_file_dup_bytes (file);

              sysprof_document_load_mountinfo (self, pid, bytes);
            }
        }
      while (egg_bitset_iter_next (&iter, &pid));
    }
}

static void
sysprof_document_load_overlays (SysprofDocument *self)
{
  EggBitsetIter iter;
  guint i;

  g_assert (SYSPROF_IS_DOCUMENT (self));

  if (egg_bitset_iter_init_first (&iter, self->overlays, &i))
    {
      do
        {
          g_autoptr(SysprofDocumentOverlay) overlay = g_list_model_get_item (G_LIST_MODEL (self), i);
          int pid = sysprof_document_frame_get_pid (SYSPROF_DOCUMENT_FRAME (overlay));
          SysprofProcessInfo *process_info = _sysprof_document_process_info (self, pid, TRUE);

          if (process_info != NULL)
            {
              const char *mount_point = sysprof_document_overlay_get_destination (overlay);
              const char *host_path = sysprof_document_overlay_get_source (overlay);
              int layer = sysprof_document_overlay_get_layer (overlay);
              g_autoptr(SysprofMount) mount = _sysprof_mount_new_for_overlay (self->strings, mount_point, host_path, layer);

              sysprof_mount_namespace_add_mount (process_info->mount_namespace,
                                                 g_steal_pointer (&mount));
            }
        }
      while (egg_bitset_iter_next (&iter, &i));
    }
}

static void
sysprof_document_load_processes (SysprofDocument *self)
{
  G_GNUC_UNUSED SysprofProcessInfo *pid0;
  g_autoptr(GHashTable) seen = NULL;
  g_autoptr(EggBitset) duplicate = NULL;
  EggBitsetIter iter;
  guint i;

  g_assert (SYSPROF_IS_DOCUMENT (self));

  seen = g_hash_table_new (NULL, NULL);
  duplicate = egg_bitset_new_empty ();

  /* Always create PID 0 info */
  pid0 = _sysprof_document_process_info (self, 0, TRUE);

  if (egg_bitset_iter_init_first (&iter, self->processes, &i))
    {
      do
        {
          g_autoptr(SysprofDocumentProcess) process = g_list_model_get_item (G_LIST_MODEL (self), i);
          int pid = sysprof_document_frame_get_pid (SYSPROF_DOCUMENT_FRAME (process));
          SysprofProcessInfo *process_info = _sysprof_document_process_info (self, pid, TRUE);
          const char *cmdline = _sysprof_document_process_get_comm (process);

          if (cmdline != NULL)
            {
              GRefString *nick = g_ref_string_acquire (process_info->fallback_symbol->binary_nick);
              g_auto(GStrv) split = g_strsplit (cmdline, " ", 2);
              const char *shared_name = split[0] ? split[0] : cmdline;

              g_clear_object (&process_info->symbol);
              process_info->symbol =
                _sysprof_symbol_new (sysprof_strings_get (self->strings, cmdline),
                                     NULL, g_steal_pointer (&nick), 0, 0,
                                     SYSPROF_SYMBOL_KIND_PROCESS);

              g_clear_object (&process_info->shared_symbol);
              process_info->shared_symbol =
                _sysprof_symbol_new (sysprof_strings_get (self->strings, shared_name),
                                     NULL, NULL, 0, 0,
                                     SYSPROF_SYMBOL_KIND_PROCESS);
            }

          /* Only include the first process discover for this process,
           * ignore all secondary notations which are just comm[] updates.
           */
          if (g_hash_table_contains (seen, GINT_TO_POINTER (pid)))
            egg_bitset_add (duplicate, i);
          else
            g_hash_table_add (seen, GINT_TO_POINTER (pid));
        }
      while (egg_bitset_iter_next (&iter, &i));
    }

  egg_bitset_subtract (self->processes, duplicate);
}

static void
sysprof_document_load_counters (SysprofDocument *self)
{
  g_autoptr(GPtrArray) counters = NULL;
  g_autoptr(EggBitset) swap_ids = NULL;
  GListModel *model;
  EggBitsetIter iter;
  guint n_counters;
  guint i;

  g_assert (SYSPROF_IS_DOCUMENT (self));

  /* Cast up front to avoid many type checks later */
  model = G_LIST_MODEL (self);

  /* If we need to swap int64 (double does not need swapping), then keep
   * a bitset of which counters need swapping. That way we have a fast
   * lookup below we can use when reading in counter values.
   */
  if (self->needs_swap)
    swap_ids = egg_bitset_new_empty ();

  /* First create our counter objects which we will use to hold values that
   * were extracted from the capture file. We create the array first so that
   * we can use g_list_store_splice() rather than have signal emission for
   * each and every added counter.
   */
  counters = g_ptr_array_new_with_free_func (g_object_unref);
  if (egg_bitset_iter_init_first (&iter, self->ctrdefs, &i))
    {
      do
        {
          g_autoptr(SysprofDocumentCtrdef) ctrdef = g_list_model_get_item (model, i);

          n_counters = sysprof_document_ctrdef_get_n_counters (ctrdef);

          for (guint j = 0; j < n_counters; j++)
            {
              g_autoptr(GArray) values = g_array_new (FALSE, FALSE, sizeof (SysprofDocumentTimedValue));
              const char *category;
              const char *name;
              const char *description;
              guint id;
              guint type;

              sysprof_document_ctrdef_get_counter (ctrdef, j, &id, &type, &category, &name, &description);

              /* Keep track if this counter will need int64 endian swaps */
              if (swap_ids != NULL && type == SYSPROF_CAPTURE_COUNTER_INT64)
                egg_bitset_add (swap_ids, id);

              g_hash_table_insert (self->counter_id_to_values,
                                   GUINT_TO_POINTER (id),
                                   g_array_ref (values));

              g_ptr_array_add (counters,
                               _sysprof_document_counter_new (id,
                                                              type,
                                                              sysprof_strings_get (self->strings, category),
                                                              sysprof_strings_get (self->strings, name),
                                                              sysprof_strings_get (self->strings, description),
                                                              g_steal_pointer (&values),
                                                              self->time_span.begin_nsec));
            }
        }
      while (egg_bitset_iter_next (&iter, &i));

      if (counters->len > 0)
        g_list_store_splice (self->counters, 0, 0, counters->pdata, counters->len);
    }

  /* Now find all the counter values and associate them with the counters
   * that were previously defined.
   */
  if (egg_bitset_iter_init_first (&iter, self->ctrsets, &i))
    {
      do
        {
          g_autoptr(SysprofDocumentCtrset) ctrset = g_list_model_get_item (model, i);
          guint n_values = sysprof_document_ctrset_get_n_values (ctrset);
          SysprofDocumentTimedValue ctrval;

          ctrval.time = sysprof_document_frame_get_time (SYSPROF_DOCUMENT_FRAME (ctrset));

          for (guint j = 0; j < n_values; j++)
            {
              GArray *values;
              guint id;

              sysprof_document_ctrset_get_raw_value (ctrset, j, &id, ctrval.v_raw);

              if (swap_ids != NULL && egg_bitset_contains (swap_ids, id))
                {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
                  ctrval.v_int64 = GINT64_FROM_BE (ctrval.v_int64);
#else
                  ctrval.v_int64 = GINT64_FROM_LE (ctrval.v_int64);
#endif
                }

              if ((values = g_hash_table_lookup (self->counter_id_to_values, GUINT_TO_POINTER (id))))
                g_array_append_val (values, ctrval);
            }
        }
      while (egg_bitset_iter_next (&iter, &i));
    }

  n_counters = g_list_model_get_n_items (G_LIST_MODEL (self->counters));

  for (i = 0; i < n_counters; i++)
    {
      g_autoptr(SysprofDocumentCounter) counter = g_list_model_get_item (G_LIST_MODEL (self->counters), i);

      _sysprof_document_counter_calculate_range (counter);
    }
}

static inline gboolean
is_data_type (SysprofCaptureFrameType type)
{
  switch (type)
    {
    case SYSPROF_CAPTURE_FRAME_ALLOCATION:
    case SYSPROF_CAPTURE_FRAME_CTRSET:
    case SYSPROF_CAPTURE_FRAME_DBUS_MESSAGE:
    case SYSPROF_CAPTURE_FRAME_EXIT:
    case SYSPROF_CAPTURE_FRAME_FORK:
    case SYSPROF_CAPTURE_FRAME_LOG:
    case SYSPROF_CAPTURE_FRAME_MAP:
    case SYSPROF_CAPTURE_FRAME_MARK:
    case SYSPROF_CAPTURE_FRAME_PROCESS:
    case SYSPROF_CAPTURE_FRAME_SAMPLE:
    case SYSPROF_CAPTURE_FRAME_TRACE:
      return TRUE;

    case SYSPROF_CAPTURE_FRAME_CTRDEF:
    case SYSPROF_CAPTURE_FRAME_FILE_CHUNK:
    case SYSPROF_CAPTURE_FRAME_JITMAP:
    case SYSPROF_CAPTURE_FRAME_METADATA:
    case SYSPROF_CAPTURE_FRAME_OVERLAY:
    case SYSPROF_CAPTURE_FRAME_TIMESTAMP:
    default:
      return FALSE;
    }
}

typedef struct _Load
{
  GMappedFile *mapped_file;
  ProgressFunc progress;
  gpointer progress_data;
  GDestroyNotify progress_data_destroy;
} Load;

static void
load_free (Load *load)
{
  g_clear_pointer (&load->mapped_file, g_mapped_file_unref);

  if (load->progress_data_destroy)
    load->progress_data_destroy (load->progress_data);

  load->progress = NULL;
  load->progress_data = NULL;
  load->progress_data_destroy = NULL;

  g_free (load);
}

static inline void
load_progress (Load       *load,
               double      fraction,
               const char *message)
{
  if (load->progress)
    load->progress (fraction, message, load->progress_data);
}

static int
sort_by_time (gconstpointer a,
              gconstpointer b,
              gpointer      base)
{
  const SysprofDocumentFramePointer *aptr = a;
  const SysprofDocumentFramePointer *bptr = b;
  const SysprofCaptureFrame *aframe = (const SysprofCaptureFrame *)((const guint8 *)base + aptr->offset);
  const SysprofCaptureFrame *bframe = (const SysprofCaptureFrame *)((const guint8 *)base + bptr->offset);

  if (aframe->time < bframe->time)
    return -1;

  if (aframe->time > bframe->time)
    return 1;

  if (aframe->type == SYSPROF_CAPTURE_FRAME_MARK && bframe->type == SYSPROF_CAPTURE_FRAME_MARK)
    {
      const SysprofCaptureMark *amark = (const SysprofCaptureMark *)aframe;
      const SysprofCaptureMark *bmark = (const SysprofCaptureMark *)bframe;

      if (amark->duration > bmark->duration)
        return -1;
      else if (amark->duration < bmark->duration)
        return 1;
    }

  return 0;
}

static int
sort_by_time_swapped (gconstpointer a,
                      gconstpointer b,
                      gpointer      base)
{
  const SysprofDocumentFramePointer *aptr = a;
  const SysprofDocumentFramePointer *bptr = b;
  const SysprofCaptureFrame *aframe = (const SysprofCaptureFrame *)((const guint8 *)base + aptr->offset);
  const SysprofCaptureFrame *bframe = (const SysprofCaptureFrame *)((const guint8 *)base + bptr->offset);
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  gint64 atime = GINT64_FROM_BE (aframe->time);
  gint64 btime = GINT64_FROM_BE (bframe->time);
#else
  gint64 atime = GINT64_FROM_LE (aframe->time);
  gint64 btime = GINT64_FROM_LE (bframe->time);
#endif

  if (atime < btime)
    return -1;

  if (atime > btime)
    return 1;

  if (aframe->type == SYSPROF_CAPTURE_FRAME_MARK && bframe->type == SYSPROF_CAPTURE_FRAME_MARK)
    {
      const SysprofCaptureMark *amark = (const SysprofCaptureMark *)aframe;
      const SysprofCaptureMark *bmark = (const SysprofCaptureMark *)bframe;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      gint64 aduration = GINT64_FROM_BE (amark->duration);
      gint64 bduration = GINT64_FROM_BE (bmark->duration);
#else
      gint64 aduration = GINT64_FROM_LE (amark->duration);
      gint64 bduration = GINT64_FROM_LE (bmark->duration);
#endif

      if (aduration > bduration)
        return -1;
      else if (aduration < bduration)
        return 1;
    }

  return 0;
}

static void
sysprof_document_update_process_exit_times (SysprofDocument *self)
{
  GHashTableIter iter;
  SysprofProcessInfo *info;

  g_assert (SYSPROF_IS_DOCUMENT (self));

  g_hash_table_iter_init (&iter, self->pid_to_process_info);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&info))
    {
      if (info->exit_time == 0)
        info->exit_time = self->time_span.end_nsec;
    }
}

static void
sysprof_document_load_cpu (SysprofDocument *self)
{
  const gsize model_len = strlen ("Model\t\t: ");
  g_autoptr(SysprofDocumentFile) file = NULL;
  g_autoptr(GBytes) bytes = NULL;
  g_autoptr(SysprofCpuInfo) cpu_info = NULL;
  g_autofree char *model = NULL;
  const char *str;
  const char *line;
  LineReader reader;
  gsize line_len;
  gsize len;

  g_assert (SYSPROF_IS_DOCUMENT (self));

  if (!(file = sysprof_document_lookup_file (self, "/proc/cpuinfo")) ||
      !(bytes = sysprof_document_file_dup_bytes (file)))
    return;

  str = (const char *)g_bytes_get_data (bytes, &len);

  line_reader_init (&reader, (char *)str, len);
  while ((line = line_reader_next (&reader, &line_len)))
    {
      if (g_str_has_prefix (line, "processor\t: "))
        {
          gint64 id = g_ascii_strtoll (line+strlen("processor\t: "), NULL, 10);

          if (cpu_info != NULL)
            g_list_store_append (self->cpu_info, cpu_info);

          g_clear_object (&cpu_info);

          cpu_info = g_object_new (SYSPROF_TYPE_CPU_INFO,
                                   "id", id,
                                   NULL);
        }

      if (g_str_has_prefix (line, "core id\t\t: "))
        {
          gint64 core_id = g_ascii_strtoll (line+strlen("core id\t\t: "), NULL, 10);

          if (core_id > 0)
            _sysprof_cpu_info_set_core_id (cpu_info, core_id);
        }

      if (g_str_has_prefix (line, "model name\t: "))
        {
          const gsize model_name_len = strlen ("model name\t: ");
          g_autofree char *model_name = g_strndup (line+model_name_len, line_len-model_name_len);

          if (cpu_info != NULL)
            _sysprof_cpu_info_set_model_name (cpu_info, model_name);
        }

      if (!model && g_str_has_prefix (line, "Model\t\t: "))
        model = g_strndup (line+model_len, line_len-model_len);
    }

  if (cpu_info != NULL)
    g_list_store_append (self->cpu_info, cpu_info);

  if (model != NULL)
    {
      guint n_items = g_list_model_get_n_items (G_LIST_MODEL (self->cpu_info));

      for (guint i = 0; i < n_items; i++)
        {
          g_autoptr(SysprofCpuInfo) item = g_list_model_get_item (G_LIST_MODEL (self->cpu_info), i);

          _sysprof_cpu_info_set_model_name (item, model);
        }
    }
}

static void
sysprof_document_load_worker (GTask        *task,
                              gpointer      source_object,
                              gpointer      task_data,
                              GCancellable *cancellable)
{
  g_autoptr(SysprofDocument) self = NULL;
  g_autoptr(GHashTable) files = NULL;
  Load *load = task_data;
  gint64 guessed_end_nsec = 0;
  goffset pos;
  gsize len;
  guint count;

  g_assert (source_object == NULL);
  g_assert (load != NULL);

  self = g_object_new (SYSPROF_TYPE_DOCUMENT, NULL);
  self->mapped_file = g_mapped_file_ref (load->mapped_file);
  self->base = (const guint8 *)g_mapped_file_get_contents (load->mapped_file);
  len = g_mapped_file_get_length (load->mapped_file);

  if (len < sizeof self->header)
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_INVALID_DATA,
                               "File header too short");
      return;
    }

  /* Keep a copy of our header */
  memcpy (&self->header, self->base, sizeof self->header);
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  self->needs_swap = !self->header.little_endian;
#else
  self->needs_swap = !!self->header.little_endian;
#endif

  self->header.time = swap_uint64 (self->needs_swap, self->header.time);
  self->header.end_time = swap_uint64 (self->needs_swap, self->header.end_time);
  self->header.capture_time[sizeof self->header.capture_time-1] = 0;

  self->time_span.begin_nsec = self->header.time;
  self->time_span.end_nsec = self->header.end_time;

  load_progress (load, .1, _("Indexing capture data frames"));

  count = 0;
  pos = sizeof self->header;
  while (pos < (len - sizeof(guint16)))
    {
      SysprofDocumentFramePointer ptr;
      guint16 frame_len;

      memcpy (&frame_len, &self->base[pos], sizeof frame_len);
      frame_len = swap_uint16 (self->needs_swap, frame_len);

      if (frame_len < sizeof (SysprofCaptureFrame))
        {
          g_warning ("Capture contained implausibly short frame");
          break;
        }

      if (frame_len % SYSPROF_CAPTURE_ALIGN != 0)
        {
          g_warning ("Capture contained frame not aligned to %u",
                     (guint)SYSPROF_CAPTURE_ALIGN);
          break;
        }

      ptr.offset = pos;
      ptr.length = frame_len;

      pos += frame_len;
      count++;

      g_array_append_val (self->frames, ptr);

      if (count % 100 == 0)
        load_progress (load,
                       (pos / (double)len) * .4,
                       _("Indexing capture data frames"));
    }

  /* Now sort all the items by their respective frame times as frames
   * cat into the writer may be tacked on at the end even though they
   * have state which belongs earlier in the capture.
   */
  if G_UNLIKELY (self->needs_swap)
    gtk_tim_sort (self->frames->data,
                  self->frames->len,
                  sizeof (SysprofDocumentFramePointer),
                  sort_by_time_swapped,
                  (gpointer)self->base);
  else
    gtk_tim_sort (self->frames->data,
                  self->frames->len,
                  sizeof (SysprofDocumentFramePointer),
                  sort_by_time,
                  (gpointer)self->base);

  for (guint f = 0; f < self->frames->len; f++)
    {
      SysprofDocumentFramePointer *ptr = &g_array_index (self->frames, SysprofDocumentFramePointer, f);
      const SysprofCaptureFrame *tainted = (const SysprofCaptureFrame *)(gpointer)&self->base[ptr->offset];
      gint64 t = swap_int64 (self->needs_swap, tainted->time);
      int pid = swap_int32 (self->needs_swap, tainted->pid);

      if (t > guessed_end_nsec && is_data_type (tainted->type))
        guessed_end_nsec = t;

      egg_bitset_add (self->pids, pid);

      switch ((int)tainted->type)
        {
        case SYSPROF_CAPTURE_FRAME_ALLOCATION:
          egg_bitset_add (self->allocations, f);
          egg_bitset_add (self->traceables, f);
          break;

        case SYSPROF_CAPTURE_FRAME_SAMPLE:
          egg_bitset_add (self->samples, f);
          egg_bitset_add (self->traceables, f);
          break;

        case SYSPROF_CAPTURE_FRAME_PROCESS:
          egg_bitset_add (self->processes, f);
          break;

        case SYSPROF_CAPTURE_FRAME_FILE_CHUNK:
          egg_bitset_add (self->file_chunks, f);
          break;

        case SYSPROF_CAPTURE_FRAME_CTRDEF:
          egg_bitset_add (self->ctrdefs, f);
          break;

        case SYSPROF_CAPTURE_FRAME_CTRSET:
          egg_bitset_add (self->ctrsets, f);
          break;

        case SYSPROF_CAPTURE_FRAME_DBUS_MESSAGE:
          egg_bitset_add (self->dbus_messages, f);
          break;

        case SYSPROF_CAPTURE_FRAME_MARK:
          egg_bitset_add (self->marks, f);
          break;

        case SYSPROF_CAPTURE_FRAME_METADATA:
          egg_bitset_add (self->metadata, f);
          break;

        case SYSPROF_CAPTURE_FRAME_JITMAP:
          egg_bitset_add (self->jitmaps, f);
          break;

        case SYSPROF_CAPTURE_FRAME_LOG:
          egg_bitset_add (self->logs, f);
          break;

        case SYSPROF_CAPTURE_FRAME_MAP:
          egg_bitset_add (self->mmaps, f);
          break;

        case SYSPROF_CAPTURE_FRAME_OVERLAY:
          egg_bitset_add (self->overlays, f);
          break;

        default:
          break;
        }

      if (tainted->type == SYSPROF_CAPTURE_FRAME_FILE_CHUNK)
        {
          const SysprofCaptureFileChunk *file_chunk = (const SysprofCaptureFileChunk *)tainted;

          if (has_null_byte (file_chunk->path, (const char *)file_chunk->data) &&
              !g_hash_table_contains (self->files_first_position, file_chunk->path))
            g_hash_table_insert (self->files_first_position,
                                 g_strdup (file_chunk->path),
                                 GUINT_TO_POINTER (f));
        }
      else if (tainted->type == SYSPROF_CAPTURE_FRAME_SAMPLE)
        {
          const SysprofCaptureSample *sample = (const SysprofCaptureSample *)tainted;
          guint n_addrs = swap_uint16 (self->needs_swap, sample->n_addrs);
          const guint8 *endptr = (const guint8 *)tainted + ptr->length;

          /* If the sample contains a context-switch, record it */
          if ((const guint8 *)sample + (n_addrs * sizeof (SysprofAddress)) <= endptr)
            {
              SysprofAddressContext last_context = SYSPROF_ADDRESS_CONTEXT_USER;

              for (guint i = 0; i < n_addrs; i++)
                {
                  SysprofAddress addr = swap_uint64 (self->needs_swap, sample->addrs[i]);

                  if (sysprof_address_is_context_switch (addr, &last_context) &&
                      last_context == SYSPROF_ADDRESS_CONTEXT_KERNEL)
                    {
                      egg_bitset_add (self->samples_with_context_switch, f);
                      break;
                    }
                }
            }

          if (sample->tid != tainted->pid)
            {
              SysprofProcessInfo *info = _sysprof_document_process_info (self, pid, TRUE);
              sysprof_process_info_seen_thread (info, swap_int32 (self->needs_swap, sample->tid));
            }
        }
      else if (tainted->type == SYSPROF_CAPTURE_FRAME_ALLOCATION)
        {
          const SysprofCaptureAllocation *alloc = (const SysprofCaptureAllocation *)tainted;
          SysprofProcessInfo *info = _sysprof_document_process_info (self, pid, TRUE);
          sysprof_process_info_seen_thread (info, swap_int32 (self->needs_swap, alloc->tid));
        }
      else if (tainted->type == SYSPROF_CAPTURE_FRAME_EXIT)
        {
          SysprofProcessInfo *info = _sysprof_document_process_info (self, pid, TRUE);

          info->exit_time = t;
        }
      else if (tainted->type == SYSPROF_CAPTURE_FRAME_MARK)
        {
          const SysprofCaptureMark *mark = (const SysprofCaptureMark *)tainted;
          const char *endptr = (const char *)tainted + ptr->length;
          gint64 duration = swap_int64 (self->needs_swap, mark->duration);

          if (t + duration > guessed_end_nsec)
            guessed_end_nsec = t + duration;

          if (has_null_byte (mark->group, endptr) &&
              has_null_byte (mark->name, endptr))
            {
              const char *group = mark->group;
              const char *name = mark->name;
              GHashTable *names;
              EggBitset *bitset;

              if (!(names = g_hash_table_lookup (self->mark_groups, group)))
                {
                  names = g_hash_table_new_full (g_str_hash,
                                                 g_str_equal,
                                                 g_free,
                                                 (GDestroyNotify)egg_bitset_unref);
                  g_hash_table_insert (self->mark_groups, g_strdup (group), names);
                }

              if (!(bitset = g_hash_table_lookup (names, name)))
                {
                  bitset = egg_bitset_new_empty ();
                  g_hash_table_insert (names, g_strdup (name), bitset);
                }

              egg_bitset_add (bitset, f);
            }
        }
    }

  if (guessed_end_nsec > self->time_span.begin_nsec)
    self->time_span.end_nsec = guessed_end_nsec;

  sysprof_document_load_cpu (self);

  load_progress (load, .6, _("Discovering file system mounts"));
  sysprof_document_load_mounts (self);

  load_progress (load, .65, _("Discovering process mount namespaces"));
  sysprof_document_load_mountinfos (self);

  load_progress (load, .7, _("Analyzing process address layouts"));
  sysprof_document_load_memory_maps (self);

  load_progress (load, .75, _("Analyzing process command line"));
  sysprof_document_load_processes (self);

  load_progress (load, .8, _("Analyzing file system overlays"));
  sysprof_document_load_overlays (self);

  load_progress (load, .85, _("Processing counters"));
  sysprof_document_load_counters (self);

  /* Ensure all our process have an exit_time set */
  sysprof_document_update_process_exit_times (self);

  g_task_return_pointer (task, g_steal_pointer (&self), g_object_unref);
}

void
_sysprof_document_new_async (GMappedFile         *mapped_file,
                             ProgressFunc         progress,
                             gpointer             progress_data,
                             GDestroyNotify       progress_data_destroy,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  Load *load;

  g_return_if_fail (mapped_file != NULL);
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  load = g_new0 (Load, 1);
  load->mapped_file = g_mapped_file_ref (mapped_file);
  load->progress = progress;
  load->progress_data = progress_data;
  load->progress_data_destroy = progress_data_destroy;

  task = g_task_new (NULL, cancellable, callback, user_data);
  g_task_set_source_tag (task, _sysprof_document_new_async);
  g_task_set_task_data (task, load, (GDestroyNotify)load_free);
  g_task_run_in_thread (task, sysprof_document_load_worker);
}

SysprofDocument *
_sysprof_document_new_finish (GAsyncResult  *result,
                              GError       **error)
{
  SysprofDocument *ret;

  g_return_val_if_fail (G_IS_TASK (result), NULL);
  ret = g_task_propagate_pointer (G_TASK (result), error);
  g_return_val_if_fail (!ret || SYSPROF_IS_DOCUMENT (ret), NULL);

  return ret;
}

GRefString *
_sysprof_document_ref_string (SysprofDocument *self,
                              const char      *name)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  return sysprof_strings_get (self->strings, name);
}

static void
sysprof_document_symbolize_symbols_cb (GObject      *object,
                                       GAsyncResult *result,
                                       gpointer      user_data)
{
  g_autoptr(SysprofDocumentSymbols) symbols = NULL;
  g_autoptr(GTask) task = user_data;
  g_autoptr(GError) error = NULL;
  SysprofDocument *self;

  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (!(symbols = _sysprof_document_symbols_new_finish (result, &error)))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  self = g_task_get_source_object (task);

  g_assert (self != NULL);
  g_assert (SYSPROF_IS_DOCUMENT (self));

  g_set_object (&self->symbols, symbols);

  g_task_return_boolean (task, TRUE);
}

typedef struct _Symbolize
{
  ProgressFunc progress_func;
  gpointer progress_data;
  GDestroyNotify progress_data_destroy;
} Symbolize;

static void
symbolize_free (Symbolize *state)
{
  if (state->progress_data_destroy)
    state->progress_data_destroy (state->progress_data);
  g_free (state);
}

static void
sysprof_document_symbolize_prepare_cb (GObject      *object,
                                       GAsyncResult *result,
                                       gpointer      user_data)
{
  SysprofSymbolizer *symbolizer = (SysprofSymbolizer *)object;
  g_autoptr(GTask) task = user_data;
  g_autoptr(GError) error = NULL;
  SysprofDocument *self;
  Symbolize *state;

  g_assert (SYSPROF_IS_SYMBOLIZER (symbolizer));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);

  g_assert (self != NULL);
  g_assert (SYSPROF_IS_DOCUMENT (self));
  g_assert (self->pid_to_process_info != NULL);

  state = g_task_get_task_data (task);

  if (!_sysprof_symbolizer_prepare_finish (symbolizer, result, &error))
    g_task_return_error (task, g_steal_pointer (&error));
  else
    _sysprof_document_symbols_new (g_task_get_source_object (task),
                                   self->strings,
                                   symbolizer,
                                   self->pid_to_process_info,
                                   state->progress_func,
                                   state->progress_data,
                                   NULL,
                                   g_task_get_cancellable (task),
                                   sysprof_document_symbolize_symbols_cb,
                                   g_object_ref (task));
}

void
_sysprof_document_symbolize_async (SysprofDocument     *self,
                                   SysprofSymbolizer   *symbolizer,
                                   ProgressFunc         progress_func,
                                   gpointer             progress_data,
                                   GDestroyNotify       progress_data_destroy,
                                   GCancellable        *cancellable,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  Symbolize *state;

  g_return_if_fail (SYSPROF_IS_DOCUMENT (self));
  g_return_if_fail (SYSPROF_IS_SYMBOLIZER (symbolizer));
  g_return_if_fail (!cancellable || SYSPROF_IS_SYMBOLIZER (symbolizer));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, _sysprof_document_symbolize_async);

  state = g_new0 (Symbolize, 1);
  state->progress_func = progress_func;
  state->progress_data = progress_data;
  state->progress_data_destroy = progress_data_destroy;
  g_task_set_task_data(task, state, (GDestroyNotify)symbolize_free);

  _sysprof_symbolizer_prepare_async (symbolizer,
                                     self,
                                     cancellable,
                                     sysprof_document_symbolize_prepare_cb,
                                     g_steal_pointer (&task));
}

gboolean
_sysprof_document_symbolize_finish (SysprofDocument  *self,
                                    GAsyncResult     *result,
                                    GError          **error)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);
  g_return_val_if_fail (g_task_is_valid (result, self), FALSE);
  g_return_val_if_fail (g_task_get_source_tag (G_TASK (result)) == _sysprof_document_symbolize_async, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

gboolean
_sysprof_document_is_native (SysprofDocument *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), FALSE);

  return self->needs_swap == FALSE;
}

/**
 * sysprof_document_lookup_file:
 * @self: a #SysprofDocument
 * @path: the path of the file
 *
 * Locates @path within the document and returns a #SysprofDocumentFile if
 * it was found which can be used to access the contents.
 *
 * Returns: (transfer full) (nullable): a #SysprofDocumentFile
 */
SysprofDocumentFile *
sysprof_document_lookup_file (SysprofDocument *self,
                              const char      *path)
{
  g_autofree char *gz_path = NULL;
  gpointer key, value;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);
  g_return_val_if_fail (path != NULL, NULL);

  gz_path = g_strdup_printf ("%s.gz", path);

  if (g_hash_table_lookup_extended (self->files_first_position, path, &key, &value) ||
      g_hash_table_lookup_extended (self->files_first_position, gz_path, &key, &value))
    {
      g_autoptr(GPtrArray) file_chunks = g_ptr_array_new_with_free_func (g_object_unref);
      const char *real_path = key;
      EggBitsetIter iter;
      guint target = GPOINTER_TO_SIZE (value);
      guint i;

      if (egg_bitset_iter_init_at (&iter, self->file_chunks, target, &i))
        {
          do
            {
              g_autoptr(SysprofDocumentFileChunk) file_chunk = sysprof_document_get_item ((GListModel *)self, i);

              if (g_strcmp0 (real_path, sysprof_document_file_chunk_get_path (file_chunk)) == 0)
                {
                  gboolean is_last = sysprof_document_file_chunk_get_is_last (file_chunk);

                  g_ptr_array_add (file_chunks, g_steal_pointer (&file_chunk));

                  if (is_last)
                    break;
                }
            }
          while (egg_bitset_iter_next (&iter, &i));
        }

      return _sysprof_document_file_new (path,
                                         g_steal_pointer (&file_chunks),
                                         g_strcmp0 (real_path, gz_path) == 0);
    }

  return NULL;
}

/**
 * sysprof_document_list_files:
 * @self: a #SysprofDocument
 *
 * Gets a #GListModel of #SysprofDocumentFile
 *
 * Returns: (transfer full): a #GListModel of #SysprofDocumentFile
 */
GListModel *
sysprof_document_list_files (SysprofDocument *self)
{
  GHashTableIter hiter;
  EggBitsetIter iter;
  GListStore *model;
  gpointer key, value;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  model = g_list_store_new (SYSPROF_TYPE_DOCUMENT_FILE);

  g_hash_table_iter_init (&hiter, self->files_first_position);
  while (g_hash_table_iter_next (&hiter, &key, &value))
    {
      g_autoptr(SysprofDocumentFile) file = NULL;
      g_autoptr(GPtrArray) file_chunks = g_ptr_array_new_with_free_func (g_object_unref);
      g_autofree char *no_gz_path = NULL;
      const char *path = key;
      guint target = GPOINTER_TO_SIZE (value);
      guint i;

      if (egg_bitset_iter_init_at (&iter, self->file_chunks, target, &i))
        {
          do
            {
              g_autoptr(SysprofDocumentFileChunk) file_chunk = sysprof_document_get_item ((GListModel *)self, i);

              if (g_strcmp0 (path, sysprof_document_file_chunk_get_path (file_chunk)) == 0)
                {
                  gboolean is_last = sysprof_document_file_chunk_get_is_last (file_chunk);

                  g_ptr_array_add (file_chunks, g_steal_pointer (&file_chunk));

                  if (is_last)
                    break;
                }
            }
          while (egg_bitset_iter_next (&iter, &i));
        }

      if (g_str_has_suffix (path, ".gz"))
        path = no_gz_path = g_strndup (path, strlen (path) - 3);

      file = _sysprof_document_file_new (path,
                                         g_steal_pointer (&file_chunks),
                                         no_gz_path != NULL);

      g_list_store_append (model, file);
    }

  return G_LIST_MODEL (model);
}

/**
 * sysprof_document_list_traceables:
 * @self: a #SysprofDocument
 *
 * Gets a #GListModel containing #SysprofDocumentTraceable found within
 * the #SysprofDocument.
 *
 * Returns: (transfer full): a #GListModel of #SysprofDocumentTraceable
 */
GListModel *
sysprof_document_list_traceables (SysprofDocument *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  return _sysprof_document_bitset_index_new (G_LIST_MODEL (self), self->traceables);
}

/**
 * sysprof_document_list_marks:
 * @self: a #SysprofDocument
 *
 * Gets a #GListModel containing #SysprofDocumentMark found within
 * the #SysprofDocument.
 *
 * Returns: (transfer full): a #GListModel of #SysprofDocumentMark
 */
GListModel *
sysprof_document_list_marks (SysprofDocument *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  return _sysprof_document_bitset_index_new (G_LIST_MODEL (self), self->marks);
}

/**
 * sysprof_document_list_marks_by_group:
 * @self: a #SysprofDocument
 * @group: the name of the group
 *
 * Gets a #GListModel containing #SysprofDocumentMark found within
 * the #SysprofDocument which are part of @group.
 *
 * Returns: (transfer full): a #GListModel of #SysprofDocumentMark
 */
GListModel *
sysprof_document_list_marks_by_group (SysprofDocument *self,
                                      const char      *group)
{
  g_autoptr(EggBitset) bitset = NULL;
  GHashTable *names;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);
  g_return_val_if_fail (group != NULL, NULL);

  bitset = egg_bitset_new_empty ();

  if ((names = g_hash_table_lookup (self->mark_groups, group)))
    {
      GHashTableIter iter;
      const char *name;
      EggBitset *indices;

      g_hash_table_iter_init (&iter, names);

      while (g_hash_table_iter_next (&iter, (gpointer *)&name, (gpointer *)&indices))
        egg_bitset_union (bitset, indices);
    }

  return _sysprof_document_bitset_index_new (G_LIST_MODEL (self), bitset);
}

/**
 * sysprof_document_list_allocations:
 * @self: a #SysprofDocument
 *
 * Gets a #GListModel containing #SysprofDocumentAllocation found within
 * the #SysprofDocument.
 *
 * Returns: (transfer full): a #GListModel of #SysprofDocumentAllocation
 */
GListModel *
sysprof_document_list_allocations (SysprofDocument *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  return _sysprof_document_bitset_index_new_full (G_LIST_MODEL (self), self->allocations, SYSPROF_TYPE_DOCUMENT_ALLOCATION);
}

/**
 * sysprof_document_list_samples:
 * @self: a #SysprofDocument
 *
 * Gets a #GListModel containing #SysprofDocumentSample found within
 * the #SysprofDocument.
 *
 * Returns: (transfer full): a #GListModel of #SysprofDocumentSample
 */
GListModel *
sysprof_document_list_samples (SysprofDocument *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  return _sysprof_document_bitset_index_new_full (G_LIST_MODEL (self), self->samples, SYSPROF_TYPE_DOCUMENT_SAMPLE);
}

/**
 * sysprof_document_list_samples_with_context_switch:
 * @self: a #SysprofDocument
 *
 * Gets a #GListModel containing #SysprofDocumentSample found within
 * the #SysprofDocument which contain a context switch.
 *
 * Returns: (transfer full): a #GListModel of #SysprofDocumentSample
 */
GListModel *
sysprof_document_list_samples_with_context_switch (SysprofDocument *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  return _sysprof_document_bitset_index_new_full (G_LIST_MODEL (self), self->samples_with_context_switch, SYSPROF_TYPE_DOCUMENT_SAMPLE);
}

/**
 * sysprof_document_list_samples_without_context_switch:
 * @self: a #SysprofDocument
 *
 * Gets a #GListModel containing #SysprofDocumentSample found within
 * the #SysprofDocument which contain a context switch.
 *
 * Returns: (transfer full): a #GListModel of #SysprofDocumentSample
 */
GListModel *
sysprof_document_list_samples_without_context_switch (SysprofDocument *self)
{
  g_autoptr(EggBitset) bitset = NULL;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  bitset = egg_bitset_copy (self->samples);
  egg_bitset_subtract (bitset, self->samples_with_context_switch);

  return _sysprof_document_bitset_index_new_full (G_LIST_MODEL (self), bitset, SYSPROF_TYPE_DOCUMENT_SAMPLE);
}

/**
 * sysprof_document_list_processes:
 * @self: a #SysprofDocument
 *
 * Gets a #GListModel containing #SysprofDocumentProcess found within
 * the #SysprofDocument.
 *
 * Returns: (transfer full): a #GListModel of #SysprofDocumentProcess
 */
GListModel *
sysprof_document_list_processes (SysprofDocument *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  return _sysprof_document_bitset_index_new (G_LIST_MODEL (self), self->processes);
}

/**
 * sysprof_document_list_jitmaps:
 * @self: a #SysprofDocument
 *
 * Gets a #GListModel containing #SysprofDocumentJitmap found within
 * the #SysprofDocument.
 *
 * Returns: (transfer full): a #GListModel of #SysprofDocumentJitmap
 */
GListModel *
sysprof_document_list_jitmaps (SysprofDocument *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  return _sysprof_document_bitset_index_new (G_LIST_MODEL (self), self->jitmaps);
}

/**
 * sysprof_document_symbolize_traceable: (skip)
 * @self: a #SysprofDocument
 * @traceable: the traceable to extract symbols for
 * @symbols: an array to store #SysprofSymbols
 * @n_symbols: the number of elements in @symbols
 * @final_context: (out) (nullable): a location to store the last address
 *   context of the stack trace.
 *
 * Batch symbolizing of a traceable.
 *
 * No ownership is transfered into @symbols and may be cheaply
 * discarded if using the stack for storage.
 *
 * Returns: The number of symbols or NULL set in @symbols.
 */
guint
sysprof_document_symbolize_traceable (SysprofDocument           *self,
                                      SysprofDocumentTraceable  *traceable,
                                      SysprofSymbol            **symbols,
                                      guint                      n_symbols,
                                      SysprofAddressContext     *final_context)
{
  SysprofAddressContext last_context = SYSPROF_ADDRESS_CONTEXT_NONE;
  const SysprofProcessInfo *process_info;
  SysprofAddress *addresses;
  guint n_addresses;
  guint n_symbolized = 0;
  int pid;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), 0);
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_TRACEABLE (traceable), 0);

  if (n_symbols == 0 || symbols == NULL)
    goto finish;

  pid = sysprof_document_frame_get_pid (SYSPROF_DOCUMENT_FRAME (traceable));
  process_info = g_hash_table_lookup (self->pid_to_process_info, GINT_TO_POINTER (pid));
  addresses = g_alloca (sizeof (SysprofAddress) * n_symbols);
  n_addresses = sysprof_document_traceable_get_stack_addresses (traceable, addresses, n_symbols);

  for (guint i = 0; i < n_addresses; i++)
    {
      SysprofAddressContext context;

      symbols[n_symbolized] = _sysprof_document_symbols_lookup (self->symbols, process_info, last_context, addresses[i]);

      if (symbols[n_symbolized] != NULL &&
          /* if we symbolized recursively, skip this one */
          (n_symbolized == 0 || symbols[n_symbolized-1] != symbols[n_symbolized]))
        n_symbolized++;

      if (sysprof_address_is_context_switch (addresses[i], &context))
        last_context = context;
    }

finish:
  if (final_context)
    *final_context = last_context;

  return n_symbolized;
}

/**
 * sysprof_document_list_logs:
 * @self: a #SysprofDocument
 *
 * Returns: (transfer full): a #GListModel of #SysprofDocumentLog
 */
GListModel *
sysprof_document_list_logs (SysprofDocument *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  return _sysprof_document_bitset_index_new (G_LIST_MODEL (self), self->logs);
}

/**
 * sysprof_document_list_metadata:
 * @self: a #SysprofDocument
 *
 * Returns: (transfer full): a #GListModel of #SysprofDocumentMetadata
 */
GListModel *
sysprof_document_list_metadata (SysprofDocument *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  return _sysprof_document_bitset_index_new (G_LIST_MODEL (self), self->metadata);
}

/**
 * sysprof_document_list_counters:
 * @self: a #SysprofDocument
 *
 * Returns: (transfer full): a #GListModel of #SysprofDocumentCounter
 */
GListModel *
sysprof_document_list_counters (SysprofDocument *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  return g_object_ref (G_LIST_MODEL (self->counters));
}

static void
sysprof_document_callgraph_cb (GObject      *object,
                               GAsyncResult *result,
                               gpointer      user_data)
{
  g_autoptr(SysprofCallgraph) callgraph = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;

  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (!(callgraph = _sysprof_callgraph_new_finish (result, &error)))
    g_task_return_error (task, g_steal_pointer (&error));
  else
    g_task_return_pointer (task, g_steal_pointer (&callgraph), g_object_unref);
}

/**
 * sysprof_document_callgraph_async:
 * @self: a #SysprofDocument
 * @flags: flags for generating the callgraph
 * @traceables: a list model of traceables for the callgraph
 * @augment_size: the size of data to reserve for augmentation in
 *   the callgraph.
 * @augment_func: (nullable) (scope notified) (closure augment_func_data):
 *   an optional callback
 *   to be executed for each node in the callgraph traces to augment
 *   as the callgraph is generated.
 * @augment_func_data: (nullable): the closure data for @augment_func
 * @augment_func_data_destroy: (destroy augment_func) (nullable): the
 *   destroy callback for @augment_func_data.
 * @cancellable: (nullable): a #GCancellable or %NULL
 * @callback: a callback to execute upon completion
 * @user_data: closure data for @callback
 *
 * Asynchronously generates a callgraph using the @traceables to get
 * the call stacks.
 *
 * Ideally you want @augment_size to be <= the size of a pointer to
 * avoid extra allocations per callgraph node.
 */
void
sysprof_document_callgraph_async (SysprofDocument         *self,
                                  SysprofCallgraphFlags    flags,
                                  GListModel              *traceables,
                                  gsize                    augment_size,
                                  SysprofAugmentationFunc  augment_func,
                                  gpointer                 augment_func_data,
                                  GDestroyNotify           augment_func_data_destroy,
                                  GCancellable            *cancellable,
                                  GAsyncReadyCallback      callback,
                                  gpointer                 user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (SYSPROF_IS_DOCUMENT (self));
  g_return_if_fail (G_IS_LIST_MODEL (traceables));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_document_callgraph_async);
  sysprof_document_mark_busy_for_task (self, task);

  _sysprof_callgraph_new_async (self,
                                flags,
                                traceables,
                                augment_size,
                                augment_func,
                                augment_func_data,
                                augment_func_data_destroy,
                                cancellable,
                                sysprof_document_callgraph_cb,
                                g_steal_pointer (&task));
}

/**
 * sysprof_document_callgraph_finish:
 * @self: a #SysprofDocument
 * @result: the #GAsyncResult provided to callback
 * @error: a location for a #GError
 *
 * Completes a request to generate a callgraph.
 *
 * Returns: (transfer full): a #SysprofCallgraph if successful; otherwise %NULL
 *   and @error is set.
 */
SysprofCallgraph *
sysprof_document_callgraph_finish (SysprofDocument  *self,
                                   GAsyncResult     *result,
                                   GError          **error)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);
  g_return_val_if_fail (G_IS_TASK (result), NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

SysprofSymbol *
_sysprof_document_process_symbol (SysprofDocument *self,
                                  int              pid,
                                  gboolean         want_shared)
{
  SysprofProcessInfo *info;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  if (pid < 0)
    pid = 0;

  info = _sysprof_document_process_info (self, pid, FALSE);

  if (info == NULL)
    return self->missing_process;

  if (want_shared && info->shared_symbol)
    return info->shared_symbol;

  if (info->symbol)
    return info->symbol;

  return info->fallback_symbol;
}

SysprofSymbol *
_sysprof_document_thread_symbol (SysprofDocument *self,
                                 int              pid,
                                 int              tid)
{
  static GRWLock rwlock;
  SysprofSymbol *ret;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  g_rw_lock_reader_lock (&rwlock);
  ret = g_hash_table_lookup (self->tid_to_symbol, GINT_TO_POINTER (tid));
  g_rw_lock_reader_unlock (&rwlock);

  if (ret == NULL)
    {
      g_rw_lock_writer_lock (&rwlock);

      if (!(ret = g_hash_table_lookup (self->tid_to_symbol, GINT_TO_POINTER (tid))))
        {
          char pidstr[32];
          char tidstr[32];

          g_snprintf (pidstr, sizeof pidstr, "(%d)", pid);

          if (tid == pid)
            g_snprintf (tidstr, sizeof tidstr, "Thread-%d (Main)", tid);
          else
            g_snprintf (tidstr, sizeof tidstr, "Thread-%d", tid);

          ret = _sysprof_symbol_new (g_ref_string_new (tidstr),
                                     NULL,
                                     g_ref_string_new (pidstr),
                                     0, 0,
                                     SYSPROF_SYMBOL_KIND_THREAD);

          g_hash_table_insert (self->tid_to_symbol, GINT_TO_POINTER (tid), ret);
        }

      g_rw_lock_writer_unlock (&rwlock);
    }

  return ret;
}

SysprofSymbol *
_sysprof_document_kernel_symbol (SysprofDocument *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  return self->symbols->context_switches[SYSPROF_ADDRESS_CONTEXT_KERNEL];
}

/**
 * sysprof_document_get_clock_at_start:
 * @self: a #SysprofDocument
 *
 * Gets the clock time when the recording started.
 *
 * Returns: the clock time, generally of `CLOCK_MONOTONIC`
 */
gint64
sysprof_document_get_clock_at_start (SysprofDocument *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), 0);

  return self->header.time;
}

/**
 * sysprof_document_list_symbols_in_traceable:
 * @self: a #SysprofDocument
 * @traceable: a #SysprofDocumentTraceable
 *
 * Gets the symbols in @traceable as a list model.
 *
 * Returns: (transfer full): a #GListModel of #SysprofSymbol
 */
GListModel *
sysprof_document_list_symbols_in_traceable (SysprofDocument          *self,
                                            SysprofDocumentTraceable *traceable)
{
  SysprofAddressContext final_context;
  GListStore *ret = NULL;
  SysprofSymbol **symbols;
  guint stack_depth;
  guint n_symbols;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_TRACEABLE (traceable), NULL);

  ret = g_list_store_new (SYSPROF_TYPE_SYMBOL);

  stack_depth = MIN (MAX_STACK_DEPTH, sysprof_document_traceable_get_stack_depth (traceable));
  symbols = g_alloca (sizeof (SysprofSymbol *) * stack_depth);
  n_symbols = sysprof_document_symbolize_traceable (self, traceable, symbols, stack_depth, &final_context);

  if (n_symbols > 0 && _sysprof_symbol_is_context_switch (symbols[0]))
    {
      symbols++;
      n_symbols--;
    }

  /* We must make a copy of the symbols because GtkListViewBase does not
   * deal with the same object being in a list gracefully. Realistically
   * we should deal with this there, but this gets things moving forward
   * for the time being.
   */
  for (guint i = 0; i < n_symbols; i++)
    {
      g_autoptr(SysprofSymbol) copy = _sysprof_symbol_copy (symbols[i]);
      g_list_store_append (ret, copy);
    }

  return G_LIST_MODEL (ret);
}

static int
str_compare (gconstpointer a,
             gconstpointer b,
             gpointer      user_data)
{
  return g_strcmp0 (*(const char * const *)a, *(const char * const *)b);
}

/**
 * sysprof_document_catalog_marks:
 * @self: a #SysprofDocument
 *
 * Get's a #GListModel of #GListModel of #SysprofMarkCatalog.
 *
 * You can use this to display sections in #GtkListView and similar
 * using #GtkFlattenListModel.
 *
 * Returns: (transfer full): a #GListModel of #GListModel of
 *   #SysprofMarkCatalog grouped by mark group.
 */
GListModel *
sysprof_document_catalog_marks (SysprofDocument *self)
{
  GListStore *store;
  GHashTableIter iter;
  gpointer key, value;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  store = g_list_store_new (G_TYPE_LIST_MODEL);

  g_hash_table_iter_init (&iter, self->mark_groups);

  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      g_autoptr(GListStore) group = NULL;
      g_autoptr(GArray) median = g_array_new (FALSE, FALSE, sizeof (gint64));
      g_autofree const char **keys = NULL;
      const char *group_name = key;
      GHashTable *names = value;
      guint len;

      group = g_list_store_new (SYSPROF_TYPE_MARK_CATALOG);

      keys = (const char **)g_hash_table_get_keys_as_array (names, &len);
      g_qsort_with_data (keys, len, sizeof (char *), (GCompareDataFunc)str_compare, NULL);

      for (guint i = 0; i < len; i++)
        {
          const char *name = keys[i];
          EggBitset *marks = g_hash_table_lookup (names, name);
          g_autoptr(GListModel) model = _sysprof_document_bitset_index_new (G_LIST_MODEL (self), marks);
          g_autoptr(SysprofMarkCatalog) names_catalog = NULL;
          gint64 min = G_MAXINT64;
          gint64 max = G_MININT64;
          gint64 total = 0;
          gint64 count = 0;
          EggBitsetIter bitset;
          guint pos;

          median->len = 0;

          if (egg_bitset_iter_init_first (&bitset, marks, &pos))
            {
              do
                {
                  const SysprofDocumentFramePointer *ptr = &g_array_index (self->frames, SysprofDocumentFramePointer, pos);
                  const SysprofCaptureMark *tainted = (const SysprofCaptureMark *)(gpointer)&self->base[ptr->offset];
                  gint64 duration = swap_int64 (self->needs_swap, tainted->duration);

                  g_array_append_val (median, duration);

                  min = MIN (min, duration);
                  max = MAX (max, duration);
                  total += duration;
                  count++;
                }
              while (egg_bitset_iter_next (&bitset, &pos));
            }

          names_catalog = _sysprof_mark_catalog_new (group_name,
                                                     name,
                                                     model,
                                                     min,
                                                     max,
                                                     count ? total/count : 0,
                                                     median->len ? g_array_index (median, gint64, median->len/2) : 0);
          g_list_store_append (group, names_catalog);
        }

      g_list_store_append (store, group);
    }

  return G_LIST_MODEL (store);
}

const SysprofTimeSpan *
sysprof_document_get_time_span (SysprofDocument *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  return &self->time_span;
}

/**
 * sysprof_document_find_counter:
 * @self: a #SysprofDocument
 * @category: the category of the counter
 * @name: the name of the counter
 *
 * Finds the first counter matching @category and @name.
 *
 * Returns: (transfer full) (nullable): a #SysprofDocumentCounter or %NULL
 */
SysprofDocumentCounter *
sysprof_document_find_counter (SysprofDocument *self,
                               const char      *category,
                               const char      *name)
{
  g_autoptr(GListModel) counters = NULL;
  guint n_items;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);
  g_return_val_if_fail (category != NULL, NULL);
  g_return_val_if_fail (name != NULL, NULL);

  counters = sysprof_document_list_counters (self);
  n_items = g_list_model_get_n_items (counters);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(SysprofDocumentCounter) counter = g_list_model_get_item (counters, i);

      if (g_strcmp0 (category, sysprof_document_counter_get_category (counter)) == 0 &&
          g_strcmp0 (name, sysprof_document_counter_get_name (counter)) == 0)
        return g_steal_pointer (&counter);
    }

  return NULL;
}

char *
sysprof_document_dup_title (SysprofDocument *self)
{
  g_autoptr(GDateTime) date_time = NULL;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  if (self->title != NULL)
    return g_strdup (self->title);

  if ((date_time = g_date_time_new_from_iso8601 (self->header.capture_time, NULL)))
    {
      g_autoptr(GDateTime) local_date_time = g_date_time_to_local(date_time);
      return g_date_time_format (local_date_time, _("Recording at %X %x"));
    }

  return g_strdup_printf (_("Recording at %s"), self->header.capture_time);
}

char *
sysprof_document_dup_subtitle (SysprofDocument *self)
{
  g_autoptr(GDateTime) date_time = NULL;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  if (self->title == NULL)
    return NULL;

  if ((date_time = g_date_time_new_from_iso8601 (self->header.capture_time, NULL)))
    {
      g_autoptr(GDateTime) local_date_time = g_date_time_to_local(date_time);
      return g_date_time_format (local_date_time, _("Recording at %X %x"));
    }

  return g_strdup_printf (_("Recording at %s"), self->header.capture_time);
}

void
_sysprof_document_set_title (SysprofDocument *self,
                             const char *title)
{
  g_return_if_fail (SYSPROF_IS_DOCUMENT (self));

  if (g_set_str (&self->title, title))
    {
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SUBTITLE]);
    }
}

/**
 * sysprof_document_list_cpu_info:
 * @self: a #SysprofDocument
 *
 * Gets the CPU that were discovered from the capture.
 *
 * Returns: (transfer full): a #GListModel of #SysprofCpuInfo
 */
GListModel *
sysprof_document_list_cpu_info (SysprofDocument *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  return g_object_ref (G_LIST_MODEL (self->cpu_info));
}

static int
sort_symbols_for_bsearch (gconstpointer a,
                          gconstpointer b)
{
  const SysprofPackedSymbol *packed_a = a;
  const SysprofPackedSymbol *packed_b = b;

  if (packed_a->pid < packed_b->pid)
    return -1;

  if (packed_a->pid > packed_b->pid)
    return 1;

  if (packed_a->addr_begin < packed_b->addr_begin)
    return -1;

  if (packed_a->addr_begin > packed_b->addr_begin)
    return 1;

  return 0;
}

static DexFuture *
sysprof_document_serialize_symbols_fiber (gpointer user_data)
{
  static const guint8 empty_string[1] = {0};
  static const SysprofPackedSymbol empty_symbol = {0};
  SysprofDocument *self = user_data;
  g_autoptr(GByteArray) strings = NULL;
  g_autoptr(GHashTable) strings_offset = NULL;
  g_autoptr(GBytes) bytes = NULL;
  g_autoptr(GArray) packed_symbols = NULL;
  GHashTableIter iter;
  gpointer value;
  char *data;
  gsize packed_len;

  g_assert (SYSPROF_IS_DOCUMENT (self));

  packed_symbols = g_array_new (FALSE, FALSE, sizeof (SysprofPackedSymbol));
  strings = g_byte_array_new ();
  strings_offset = g_hash_table_new (g_str_hash, g_str_equal);

  /* Always put empty string at head so 0 is never a valid
   * offset for the document entries except for "".
   */
  g_byte_array_append (strings, empty_string, sizeof empty_string);
  g_hash_table_insert (strings_offset, "", 0);

  g_hash_table_iter_init (&iter, self->pid_to_process_info);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      const SysprofProcessInfo *process_info = value;

      if (process_info->symbol_cache != NULL)
        sysprof_symbol_cache_populate_packed (process_info->symbol_cache,
                                              packed_symbols,
                                              strings,
                                              strings_offset,
                                              process_info->pid);
    }

  gtk_tim_sort (packed_symbols->data,
                packed_symbols->len,
                sizeof (SysprofPackedSymbol),
                (GCompareDataFunc)sort_symbols_for_bsearch,
                NULL);
  g_array_append_val (packed_symbols, empty_symbol);

  packed_len = sizeof (SysprofPackedSymbol) * packed_symbols->len;

  /* Update the offsets to be relative to the beginning of the
   * section containing our packed symbols. Ignore the last symbol
   * so that it stays all zero.
   */
  for (guint i = 0; i < packed_symbols->len-1; i++)
    {
      g_array_index (packed_symbols, SysprofPackedSymbol, i).offset += packed_len;
      g_array_index (packed_symbols, SysprofPackedSymbol, i).tag_offset += packed_len;
    }

  if (G_MAXSSIZE - packed_len < strings->len)
    return dex_future_new_for_errno (ENOMEM);

  data = g_malloc (packed_len + strings->len);
  memcpy (data, packed_symbols->data, packed_len);
  memcpy (data+packed_len, strings->data, strings->len);

  bytes = g_bytes_new_take (data, packed_len + strings->len);

  return dex_future_new_take_boxed (G_TYPE_BYTES, g_steal_pointer (&bytes));
}

void
sysprof_document_serialize_symbols_async (SysprofDocument     *self,
                                          GCancellable        *cancellable,
                                          GAsyncReadyCallback  callback,
                                          gpointer             user_data)
{
  g_autoptr(DexAsyncResult) result = NULL;

  g_return_if_fail (SYSPROF_IS_DOCUMENT (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  result = dex_async_result_new (self, cancellable, callback, user_data);

  dex_async_result_await (result,
                          dex_scheduler_spawn (dex_thread_pool_scheduler_get_default (), 0,
                                               sysprof_document_serialize_symbols_fiber,
                                               g_object_ref (self),
                                               g_object_unref));
}

/**
 * sysprof_document_serialize_symbols_finish:
 * @self: a #SysprofDocument
 * @result: a #GAsyncResult
 * @error: a location for a #GError
 *
 * Completes a request to serialize the symbols of the document
 * encoded in a format that Sysprof understands.
 *
 * Returns: (transfer full): a #GBytes if successful; otherwise %NULL
 *   and @error is set.
 */
GBytes *
sysprof_document_serialize_symbols_finish (SysprofDocument  *self,
                                           GAsyncResult     *result,
                                           GError          **error)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);
  g_return_val_if_fail (DEX_IS_ASYNC_RESULT (result), NULL);

  return dex_async_result_propagate_pointer (DEX_ASYNC_RESULT (result), error);
}

/**
 * sysprof_document_lookup_process:
 * @self: a #SysprofDocument
 * @pid: the process identifier
 *
 * Looks up a process by PID.
 *
 * If multiple copies of the process exist, the result is undefined.
 *
 * Returns: (transfer full) (nullable): a #SysprofDocumentProcess or %NULL
 */
SysprofDocumentProcess *
sysprof_document_lookup_process (SysprofDocument *self,
                                 int              pid)
{
  EggBitsetIter iter;
  guint position;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  if (egg_bitset_iter_init_first (&iter, self->processes, &position))
    {
      do
        {
          g_autoptr(SysprofDocumentProcess) process = g_list_model_get_item (G_LIST_MODEL (self), position);

          g_assert (SYSPROF_IS_DOCUMENT_PROCESS (process));

          if (sysprof_document_frame_get_pid (SYSPROF_DOCUMENT_FRAME (process)) == pid)
            return g_steal_pointer (&process);
        }
      while (egg_bitset_iter_next (&iter, &position));
    }

  return NULL;
}

/**
 * sysprof_document_list_dbus_messages:
 * @self: a #SysprofDocument
 *
 * Gets a #GListModel of #SysprofDocumentDBusMessage.
 *
 * Returns: (transfer full): a #GListModel
 */
GListModel *
sysprof_document_list_dbus_messages (SysprofDocument *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  return _sysprof_document_bitset_index_new (G_LIST_MODEL (self), self->dbus_messages);
}

static void
sysprof_document_save_cb (GObject      *object,
                          GAsyncResult *result,
                          gpointer      user_data)
{
  GFile *file = (GFile *)object;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;

  g_assert (G_IS_FILE (file));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (!g_file_replace_contents_finish (file, result, NULL, &error))
    g_task_return_error (task, g_steal_pointer (&error));
  else
    g_task_return_boolean (task, TRUE);
}

/**
 * sysprof_document_save_async:
 * @self: a #SysprofDocument
 * @file: a #GFile
 * @cancellable: (nullable): a #GCancellable or %NULL
 *
 */
void
sysprof_document_save_async (SysprofDocument     *self,
                             GFile               *file,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  g_autoptr(GBytes) bytes = NULL;
  g_autofree char *basename = NULL;

  g_return_if_fail (SYSPROF_IS_DOCUMENT (self));
  g_return_if_fail (G_IS_FILE (file));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, sysprof_document_save_async);
  sysprof_document_mark_busy_for_task (self, task);

  basename = g_file_get_basename (file);
  _sysprof_document_set_title (self, basename);

  bytes = g_mapped_file_get_bytes (self->mapped_file);

  g_file_replace_contents_bytes_async (file,
                                       bytes,
                                       NULL,
                                       FALSE,
                                       G_FILE_CREATE_REPLACE_DESTINATION,
                                       cancellable,
                                       sysprof_document_save_cb,
                                       g_steal_pointer (&task));
}

gboolean
sysprof_document_save_finish (SysprofDocument  *self,
                              GAsyncResult     *result,
                              GError          **error)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

GArray *
_sysprof_document_get_frames (SysprofDocument *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  return g_array_ref (self->frames);
}

EggBitset *
_sysprof_document_get_allocations (SysprofDocument *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), NULL);

  return egg_bitset_ref (self->allocations);
}

gboolean
sysprof_document_get_busy (SysprofDocument *self)
{
  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (self), FALSE);

  return self->busy_count > 0;
}

static void
sysprof_document_serialize_symbols_cb (GObject      *object,
                                       GAsyncResult *result,
                                       gpointer      user_data)
{
  SysprofDocument *document = (SysprofDocument *)object;
  g_autoptr(DexPromise) promise = user_data;
  g_autoptr(GBytes) bytes = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (SYSPROF_IS_DOCUMENT (document));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (DEX_IS_PROMISE (promise));

  if ((bytes = sysprof_document_serialize_symbols_finish (document, result, &error)))
    {
      GValue value = G_VALUE_INIT;

      g_value_init (&value, G_TYPE_BYTES);
      g_value_take_boxed (&value, g_steal_pointer (&bytes));

      dex_promise_resolve (promise, &value);
    }
  else
    dex_promise_reject (promise, g_steal_pointer (&error));
}

DexFuture *
_sysprof_document_serialize_symbols (SysprofDocument *document)
{
  DexPromise *promise;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT (document), NULL);

  promise = dex_promise_new ();
  sysprof_document_serialize_symbols_async (document,
                                            NULL,
                                            sysprof_document_serialize_symbols_cb,
                                            dex_ref (promise));
  return DEX_FUTURE (promise);
}
