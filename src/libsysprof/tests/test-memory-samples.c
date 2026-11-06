/* test-memory-samples.c
 *
 * Copyright 2026 Christian Hergert <christian@sourceandstack.com>
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

#include <glib/gstdio.h>
#include <sysprof.h>

#include "sysprof-memory-sampler-private.h"
#include "sysprof-symbolizer-private.h"

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureWriter, sysprof_capture_writer_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureReader, sysprof_capture_reader_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SysprofCaptureCursor, sysprof_capture_cursor_unref)

typedef struct
{
  SysprofSymbolizer parent_instance;
  gboolean          saw_ip;
  gboolean          saw_stack;
} TestMemorySymbolizer;

typedef struct
{
  SysprofSymbolizerClass parent_class;
} TestMemorySymbolizerClass;

G_DEFINE_FINAL_TYPE (TestMemorySymbolizer, test_memory_symbolizer, SYSPROF_TYPE_SYMBOLIZER)

static SysprofSymbol *
test_memory_symbolizer_symbolize (SysprofSymbolizer        *symbolizer,
                                  SysprofStrings           *strings,
                                  const SysprofProcessInfo *process_info,
                                  SysprofAddressContext     context,
                                  SysprofAddress            address)
{
  TestMemorySymbolizer *self = (TestMemorySymbolizer *)symbolizer;

  g_assert_cmpuint (address, !=, 0xfedcba9876543210);

  self->saw_ip |= address == 0x100000001;
  self->saw_stack |= address == 0x123456789abcdef0;

  return NULL;
}

static void
test_memory_symbolizer_class_init (TestMemorySymbolizerClass *klass)
{
  SysprofSymbolizerClass *symbolizer_class = SYSPROF_SYMBOLIZER_CLASS (klass);

  symbolizer_class->symbolize = test_memory_symbolizer_symbolize;
}

static void
test_memory_symbolizer_init (TestMemorySymbolizer *self)
{
}

static char *
create_capture (gboolean metadata)
{
  g_autoptr(SysprofCaptureWriter) writer = NULL;
  SysprofCaptureMemorySample sample = { 0 };
  SysprofCaptureAddress stack[] = { (guint64)-512, 0x123456789abcdef0, 0x1122334455667788 };
  SysprofCaptureStat stat;
  char *path = NULL;
  int fd = g_file_open_tmp ("sysprof-memory-XXXXXX.syscap", &path, NULL);

  g_assert_cmpint (fd, >=, 0);

  writer = sysprof_capture_writer_new_from_fd (fd, 0);

  sample.frame.time = SYSPROF_CAPTURE_CURRENT_TIME;
  sample.frame.pid = 123;
  sample.frame.cpu = 2;
  sample.tid = 124;
  sample.event_id = sysprof_capture_writer_request_memory_event (writer);
  sample.ip = 0x100000001;
  sample.address = 0xfedcba9876543210;
  sample.period = 1000;
  sample.weight = 1236;
  sample.data_src = 0x808000001;
  sample.time_enabled = 100000;
  sample.time_running = 50000;
  sample.weight_type = SYSPROF_CAPTURE_MEMORY_WEIGHT_PERF;
  sample.flags = (SYSPROF_CAPTURE_MEMORY_SAMPLE_IP | SYSPROF_CAPTURE_MEMORY_SAMPLE_ADDR |
                  SYSPROF_CAPTURE_MEMORY_SAMPLE_PERIOD | SYSPROF_CAPTURE_MEMORY_SAMPLE_WEIGHT |
                  SYSPROF_CAPTURE_MEMORY_SAMPLE_DATA_SRC | SYSPROF_CAPTURE_MEMORY_SAMPLE_EXACT_IP |
                  SYSPROF_CAPTURE_MEMORY_SAMPLE_TIME_ENABLED | SYSPROF_CAPTURE_MEMORY_SAMPLE_TIME_RUNNING);

  sample.n_addrs = G_N_ELEMENTS (stack);

  if (metadata)
    g_assert_true (sysprof_capture_writer_add_metadata (writer, sample.frame.time, -1, -1,
                                                       SYSPROF_CAPTURE_MEMORY_EVENT_PREFIX "0000000000000001",
                                                       "[event]\nversion=1\nname=mem-loads\n", -1));

  g_assert_true (sysprof_capture_writer_add_memory_sample (writer, &sample, stack));

  sample.frame.time++;
  sample.weight = 0;
  sample.address = 0;
  sample.n_addrs = 0;
  g_assert_true (sysprof_capture_writer_add_memory_sample (writer, &sample, NULL));

  sample.frame.time++;
  sample.flags = SYSPROF_CAPTURE_MEMORY_SAMPLE_IP;
  sample.weight_type = SYSPROF_CAPTURE_MEMORY_WEIGHT_NONE;
  sample.event_id = sysprof_capture_writer_request_memory_event (writer);

  if (metadata)
    g_assert_true (sysprof_capture_writer_add_metadata (writer, sample.frame.time, -1, -1,
                                                       SYSPROF_CAPTURE_MEMORY_EVENT_PREFIX "0000000000000002",
                                                       "[event]\nversion=1\nname=other\n", -1));

  g_assert_true (sysprof_capture_writer_add_memory_sample (writer, &sample, NULL));

  sample.n_addrs = SYSPROF_CAPTURE_MEMORY_SAMPLE_MAX_STACK + 1;
  g_assert_false (sysprof_capture_writer_add_memory_sample (writer, &sample, stack));

  g_assert_true (sysprof_capture_writer_add_sample (writer, sample.frame.time, 2, 123, 124,
                                                   stack, G_N_ELEMENTS (stack)));

  sysprof_capture_writer_stat (writer, &stat);
  g_assert_cmpuint (stat.frame_count[SYSPROF_CAPTURE_FRAME_SAMPLE], ==, 1);
  g_assert_true (sysprof_capture_writer_flush (writer));

  return path;
}

static void
swap_capture (const char *path)
{
  g_autofree char *bytes = NULL;
  gsize len;
  SysprofCaptureFileHeader *header;

  g_assert_true (g_file_get_contents (path, &bytes, &len, NULL));

  header = (SysprofCaptureFileHeader *)bytes;
  header->little_endian = !header->little_endian;
  header->time = GUINT64_SWAP_LE_BE (header->time);
  header->end_time = GUINT64_SWAP_LE_BE (header->end_time);
  header->frame_index.last_frame_index = GUINT64_SWAP_LE_BE (header->frame_index.last_frame_index);

  for (gsize pos = sizeof *header; pos < len;)
    {
      SysprofCaptureFrame *frame = (SysprofCaptureFrame *)(bytes + pos);
      guint16 size = frame->len;

      if (frame->type == SYSPROF_CAPTURE_FRAME_MEMORY_SAMPLE)
        {
          SysprofCaptureMemorySample *s = (SysprofCaptureMemorySample *)frame;

          for (guint i = 0; i < s->n_addrs; i++)
            s->addrs[i] = GUINT64_SWAP_LE_BE (s->addrs[i]);
#define SWAP32(field) s->field = GUINT32_SWAP_LE_BE (s->field)
#define SWAP64(field) s->field = GUINT64_SWAP_LE_BE (s->field)
          SWAP32 (tid);
          SWAP32 (flags);
          SWAP64 (event_id);
          SWAP64 (ip);
          SWAP64 (address);
          SWAP64 (period);
          SWAP64 (weight);
          SWAP64 (data_src);
          SWAP64 (time_enabled);
          SWAP64 (time_running);
          s->n_addrs = GUINT16_SWAP_LE_BE (s->n_addrs);
          s->weight_type = GUINT16_SWAP_LE_BE (s->weight_type);
#undef SWAP32
#undef SWAP64
        }
      else if (frame->type == SYSPROF_CAPTURE_FRAME_SAMPLE)
        {
          SysprofCaptureSample *s = (SysprofCaptureSample *)frame;

          for (guint i = 0; i < s->n_addrs; i++)
            s->addrs[i] = GUINT64_SWAP_LE_BE (s->addrs[i]);

          s->n_addrs = GUINT16_SWAP_LE_BE (s->n_addrs);
          s->tid = GUINT32_SWAP_LE_BE (s->tid);
        }
      else if (frame->type == SYSPROF_CAPTURE_FRAME_TIMESTAMP &&
               size == sizeof (SysprofCaptureFrameIndex))
        {
          SysprofCaptureFrameIndex *index = (SysprofCaptureFrameIndex *)frame;

          index->previous_frame_index = GUINT64_SWAP_LE_BE (index->previous_frame_index);
        }

      frame->len = GUINT16_SWAP_LE_BE (frame->len);
      frame->cpu = GUINT16_SWAP_LE_BE (frame->cpu);
      frame->pid = GUINT32_SWAP_LE_BE (frame->pid);
      frame->time = GUINT64_SWAP_LE_BE (frame->time);
      frame->padding2 = GUINT32_SWAP_LE_BE (frame->padding2);
      pos += size;
    }

  g_assert_true (g_file_set_contents (path, bytes, len, NULL));
}

static bool
count_memory (const SysprofCaptureFrame *frame,
              void                      *data)
{
  guint *count = data;

  g_assert_cmpuint (frame->type, ==, SYSPROF_CAPTURE_FRAME_MEMORY_SAMPLE);
  (*count)++;

  return TRUE;
}

static void
callgraph_ready (GObject      *object,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(SysprofCallgraph) callgraph = NULL;

  callgraph = sysprof_document_callgraph_finish (SYSPROF_DOCUMENT (object), result, &error);
  g_assert_no_error (error);
  g_assert_nonnull (callgraph);
  g_assert_cmpuint (g_list_model_get_n_items (G_LIST_MODEL (callgraph)), >, 0);

  g_main_loop_quit (user_data);
}

static void
test_roundtrip (gconstpointer data)
{
  g_autofree char *path = create_capture (TRUE);
  g_autoptr(SysprofCaptureReader) reader = NULL;
  g_autoptr(SysprofCaptureCursor) cursor = NULL;
  g_autoptr(SysprofDocumentLoader) loader = NULL;
  g_autoptr(SysprofSymbolizer) symbolizer = NULL;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(GListModel) memory = NULL;
  g_autoptr(GListModel) cpu = NULL;
  g_autoptr(SysprofDocumentMemorySample) sample = NULL;
  g_autoptr(GMainLoop) loop = NULL;
  g_autoptr(GError) error = NULL;
  SysprofCaptureFrameType type = SYSPROF_CAPTURE_FRAME_MEMORY_SAMPLE;
  const SysprofCaptureMemorySample *raw;
  SysprofCaptureFrame frame;
  guint count = 0;
  guint64 stack[3];

  if (GPOINTER_TO_INT (data))
    swap_capture (path);

  reader = sysprof_capture_reader_new (path);
  g_assert_nonnull (reader);

  while (sysprof_capture_reader_peek_frame (reader, &frame) && frame.type != type)
    g_assert_true (sysprof_capture_reader_skip (reader));

  raw = sysprof_capture_reader_read_memory_sample (reader);
  g_assert_nonnull (raw);
  g_assert_cmpint (raw->tid, ==, 124);
  g_assert_cmpint (raw->frame.pid, ==, 123);
  g_assert_cmpint (raw->frame.cpu, ==, 2);
  g_assert_cmpuint (raw->ip, ==, 0x100000001);
  g_assert_cmpuint (raw->address, ==, 0xfedcba9876543210);
  g_assert_cmpuint (raw->weight, ==, 1236);
  g_assert_cmpuint (raw->period, ==, 1000);
  g_assert_cmpuint (raw->data_src, ==, 0x808000001);
  g_assert_cmpuint (raw->time_enabled, ==, 100000);
  g_assert_cmpuint (raw->time_running, ==, 50000);
  g_assert_cmpuint (raw->n_addrs, ==, 3);
  g_assert_cmpuint (raw->addrs[1], ==, 0x123456789abcdef0);

  sysprof_capture_reader_reset (reader);
  cursor = sysprof_capture_cursor_new (reader);
  sysprof_capture_cursor_add_condition (cursor, sysprof_capture_condition_new_where_type_in (1, &type));
  sysprof_capture_cursor_foreach (cursor, count_memory, &count);
  g_assert_cmpuint (count, ==, 3);

  loader = sysprof_document_loader_new (path);
  symbolizer = g_object_new (test_memory_symbolizer_get_type (), NULL);
  sysprof_document_loader_set_symbolizer (loader, symbolizer);
  document = sysprof_document_loader_load (loader, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (document);
  g_assert_true (((TestMemorySymbolizer *)symbolizer)->saw_ip);
  g_assert_true (((TestMemorySymbolizer *)symbolizer)->saw_stack);

  memory = sysprof_document_list_memory_samples (document);
  cpu = sysprof_document_list_samples (document);
  g_assert_cmpuint (g_list_model_get_n_items (memory), ==, 3);
  g_assert_cmpuint (g_list_model_get_n_items (cpu), ==, 1);

  sample = g_list_model_get_item (memory, 0);
  g_assert_cmpint (sysprof_document_memory_sample_get_tid (sample), ==, 124);
  g_assert_cmpuint (sysprof_document_memory_sample_get_ip (sample), ==, 0x100000001);
  g_assert_cmpuint (sysprof_document_memory_sample_get_address (sample), ==, 0xfedcba9876543210);
  g_assert_cmpuint (sysprof_document_memory_sample_get_weight (sample), ==, 1236);
  g_assert_cmpuint (sysprof_document_traceable_get_stack_addresses (SYSPROF_DOCUMENT_TRACEABLE (sample), stack, 3), ==, 3);
  g_assert_cmpuint (stack[1], ==, 0x123456789abcdef0);
  g_assert_cmpuint (sysprof_document_traceable_get_stack_address (SYSPROF_DOCUMENT_TRACEABLE (sample), 1), ==, stack[1]);

  g_clear_object (&sample);
  sample = g_list_model_get_item (memory, 1);
  g_assert_cmpuint (sysprof_document_memory_sample_get_weight (sample), ==, 0);
  g_assert_true (sysprof_document_memory_sample_get_flags (sample) & SYSPROF_CAPTURE_MEMORY_SAMPLE_WEIGHT);
  g_assert_true (sysprof_document_memory_sample_get_flags (sample) & SYSPROF_CAPTURE_MEMORY_SAMPLE_ADDR);

  g_clear_object (&sample);
  sample = g_list_model_get_item (memory, 2);
  g_assert_false (sysprof_document_memory_sample_get_flags (sample) & SYSPROF_CAPTURE_MEMORY_SAMPLE_WEIGHT);

  loop = g_main_loop_new (NULL, FALSE);
  sysprof_document_callgraph_async (document,
                                    0,
                                    memory,
                                    0,
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL,
                                    callgraph_ready,
                                    loop);

  g_main_loop_run (loop);
  g_assert_cmpint (g_unlink (path), ==, 0);
}

static void
test_cat (gconstpointer data)
{
  g_autofree char *source = create_capture (TRUE);
  g_autofree char *dest = NULL;
  g_autoptr(SysprofCaptureReader) reader = NULL;
  g_autoptr(SysprofCaptureWriter) writer = NULL;
  SysprofCaptureFrame frame;
  gboolean ids[5] = { 0 };
  guint samples = 0;
  guint descriptions = 0;
  int fd = g_file_open_tmp ("sysprof-memory-cat-XXXXXX", &dest, NULL);

  if (GPOINTER_TO_INT (data) == 2)
    swap_capture (source);

  reader = sysprof_capture_reader_new (source);
  writer = sysprof_capture_writer_new_from_fd (fd, 0);

  if (GPOINTER_TO_INT (data) == 1)
    {
      g_assert_true (sysprof_capture_reader_splice (reader, writer));
      g_assert_true (sysprof_capture_reader_splice (reader, writer));
    }
  else
    {
      g_assert_true (sysprof_capture_writer_cat (writer, reader));
      g_assert_true (sysprof_capture_writer_cat (writer, reader));
    }

  g_clear_pointer (&writer, sysprof_capture_writer_unref);
  g_clear_pointer (&reader, sysprof_capture_reader_unref);
  reader = sysprof_capture_reader_new (dest);

  while (sysprof_capture_reader_peek_frame (reader, &frame))
    {
      if (frame.type == SYSPROF_CAPTURE_FRAME_MEMORY_SAMPLE)
        {
          const SysprofCaptureMemorySample *sample = sysprof_capture_reader_read_memory_sample (reader);

          g_assert_nonnull (sample);
          g_assert_cmpuint (sample->event_id, >, 0);
          g_assert_cmpuint (sample->event_id, <=, 4);
          g_assert_true (ids[sample->event_id]);
          samples++;
        }
      else if (frame.type == SYSPROF_CAPTURE_FRAME_METADATA)
        {
          const SysprofCaptureMetadata *metadata = sysprof_capture_reader_read_metadata (reader);
          guint64 id;

          g_assert_nonnull (metadata);
          id = g_ascii_strtoull (metadata->id + strlen (SYSPROF_CAPTURE_MEMORY_EVENT_PREFIX),
                                 NULL,
                                 16);

          g_assert_cmpuint (id, >, 0);
          g_assert_cmpuint (id, <=, 4);
          ids[id] = TRUE;
          descriptions++;
        }
      else
        g_assert_true (sysprof_capture_reader_skip (reader));
    }

  g_assert_cmpuint (samples, ==, 6);
  g_assert_cmpuint (descriptions, ==, 4);

  g_unlink (source);
  g_unlink (dest);
}

static void
test_perf_parser (void)
{
  struct {
    struct perf_event_header header;
    guint64                  identifier;
    guint64                  ip;
    guint32 pid, tid;
    guint64 time, address, period;
    guint64 value, enabled, running;
    guint64 nr, stack[3];
    guint64 weight, data_src;
  } event = {
    .header = { .type = PERF_RECORD_SAMPLE, .misc = PERF_RECORD_MISC_EXACT_IP, .size = sizeof event },
    .identifier = 999,
    .ip = 0x123456789abcdef0,
    .pid = 123,
    .tid = 124,
    .time = 1000000000,
    .address = 0,
    .period = 1000,
    .value = 5000,
    .enabled = 10000,
    .running = 9000,
    .nr = 3,
    .stack = { (guint64)-512, 0x123400000001, 0x234500000001 },
    .weight = 0,
    .data_src = 0x100000001,
  };
  SysprofCaptureMemorySample sample;
  SysprofCaptureAddress stack[SYSPROF_CAPTURE_MEMORY_SAMPLE_MAX_STACK];

  g_assert_true (_sysprof_memory_sample_decode (&event, sizeof event, SYSPROF_MEMORY_SAMPLE_TYPE,
                                                SYSPROF_MEMORY_READ_FORMAT, &sample, stack));

  g_assert_cmpuint (sample.ip, ==, event.ip);
  g_assert_cmpuint (sample.period, ==, 1000);
  g_assert_cmpuint (sample.time_enabled, ==, 10000);
  g_assert_cmpuint (sample.time_running, ==, 9000);
  g_assert_cmpuint (sample.weight, ==, 0);
  g_assert_cmpuint (sample.weight_type, ==, SYSPROF_CAPTURE_MEMORY_WEIGHT_PERF);
  g_assert_true (sample.flags & SYSPROF_CAPTURE_MEMORY_SAMPLE_WEIGHT);
  g_assert_true (sample.flags & SYSPROF_CAPTURE_MEMORY_SAMPLE_EXACT_IP);
  g_assert_true (sample.flags & SYSPROF_CAPTURE_MEMORY_SAMPLE_ADDR);
  g_assert_cmpuint (stack[1], ==, event.stack[1]);

  for (gsize size = 0; size < sizeof event; size++)
    {
      event.header.size = size;
      g_assert_false (_sysprof_memory_sample_decode (&event, size, SYSPROF_MEMORY_SAMPLE_TYPE,
                                                     SYSPROF_MEMORY_READ_FORMAT, &sample, stack));
    }

  event.header.size = sizeof event;
  event.nr = G_MAXUINT64;
  g_assert_false (_sysprof_memory_sample_decode (&event, sizeof event, SYSPROF_MEMORY_SAMPLE_TYPE,
                                                 SYSPROF_MEMORY_READ_FORMAT, &sample, stack));

  event.nr = 3;
  event.weight = 0x3344112255667788;
  g_assert_true (_sysprof_memory_sample_decode (&event, sizeof event,
                                                (SYSPROF_MEMORY_SAMPLE_TYPE & ~PERF_SAMPLE_WEIGHT) | PERF_SAMPLE_WEIGHT_STRUCT,
                                                SYSPROF_MEMORY_READ_FORMAT, &sample, stack));

  g_assert_cmpuint (sample.weight_type, ==, SYSPROF_CAPTURE_MEMORY_WEIGHT_PERF_STRUCT);
  g_assert_cmpuint (sample.weight, ==, event.weight);
  g_assert_false (_sysprof_memory_sample_decode (&event, sizeof event,
                                                 SYSPROF_MEMORY_SAMPLE_TYPE | PERF_SAMPLE_RAW,
                                                 SYSPROF_MEMORY_READ_FORMAT, &sample, stack));
}

static void
test_perf_optional (void)
{
  struct {
    struct perf_event_header header;
    guint32 pid, tid;
    guint64                  time;
  } minimal = {
    .header = { .type = PERF_RECORD_SAMPLE, .size = sizeof minimal },
    .pid = 123,
    .tid = 124,
    .time = 1000,
  };
  struct {
    struct perf_event_header header;
    guint32 pid, tid;
    guint64 time, nr, stack[300];
  } deep = {
    .header = { .type = PERF_RECORD_SAMPLE, .size = sizeof deep },
    .pid = 123,
    .tid = 124,
    .time = 1000,
    .nr = 300,
  };
  SysprofCaptureMemorySample sample;
  SysprofCaptureAddress stack[SYSPROF_CAPTURE_MEMORY_SAMPLE_MAX_STACK];

  g_assert_true (_sysprof_memory_sample_decode (&minimal, sizeof minimal,
                                                (PERF_SAMPLE_TID | PERF_SAMPLE_TIME),
                                                0, &sample, stack));

  g_assert_cmpuint (sample.flags, ==, 0);
  g_assert_cmpuint (sample.weight_type, ==, SYSPROF_CAPTURE_MEMORY_WEIGHT_NONE);
  g_assert_cmpuint (sample.n_addrs, ==, 0);

  g_assert_true (_sysprof_memory_sample_decode (&deep, sizeof deep,
                                                (PERF_SAMPLE_TID | PERF_SAMPLE_TIME | PERF_SAMPLE_CALLCHAIN),
                                                0, &sample, stack));

  g_assert_cmpuint (sample.n_addrs, ==, SYSPROF_CAPTURE_MEMORY_SAMPLE_MAX_STACK);
  g_assert_true (sample.flags & SYSPROF_CAPTURE_MEMORY_SAMPLE_STACK_TRUNCATED);
}

static void
test_invalid_capture (void)
{
  g_autofree char *path = create_capture (FALSE);
  g_autofree char *bytes = NULL;
  gsize len;
  SysprofCaptureMemorySample *sample = NULL;
  gsize offset = 0;

  g_assert_true (g_file_get_contents (path, &bytes, &len, NULL));

  for (gsize pos = sizeof (SysprofCaptureFileHeader); pos < len;)
    {
      SysprofCaptureFrame *frame = (SysprofCaptureFrame *)(bytes + pos);

      if (frame->type == SYSPROF_CAPTURE_FRAME_MEMORY_SAMPLE)
        {
          sample = (SysprofCaptureMemorySample *)frame;
          offset = pos;
          break;
        }

      pos += frame->len;
    }

  g_assert_nonnull (sample);

  for (guint variant = 0; variant < 4; variant++)
    {
      g_autoptr(SysprofCaptureReader) reader = NULL;
      SysprofCaptureFrame frame;

      sample->frame.len = sizeof *sample + 3 * sizeof (guint64);
      sample->n_addrs = 3;

      if (variant == 0)
        sample->n_addrs = 257;
      else if (variant == 1)
        sample->frame.len = sizeof (SysprofCaptureFrame);
      else if (variant == 2)
        sample->frame.len = 65528;

      g_assert_true (g_file_set_contents (path, bytes, variant == 3 ? offset + sizeof *sample : len, NULL));
      reader = sysprof_capture_reader_new (path);
      g_assert_nonnull (reader);

      while (sysprof_capture_reader_peek_frame (reader, &frame) && frame.type != SYSPROF_CAPTURE_FRAME_MEMORY_SAMPLE)
        g_assert_true (sysprof_capture_reader_skip (reader));

      g_assert_null (sysprof_capture_reader_read_memory_sample (reader));
      {
        g_autoptr(SysprofDocumentLoader) loader = sysprof_document_loader_new (path);
        g_autoptr(SysprofDocument) document = NULL;
        g_autoptr(GListModel) memory = NULL;
        g_autoptr(GError) error = NULL;

        sysprof_document_loader_set_symbolizer (loader, sysprof_no_symbolizer_get ());
        g_test_expect_message (NULL, G_LOG_LEVEL_WARNING, "*invalid or truncated frame*");
        document = sysprof_document_loader_load (loader, NULL, &error);
        g_test_assert_expected_messages ();
        g_assert_no_error (error);
        g_assert_nonnull (document);

        memory = sysprof_document_list_memory_samples (document);
        g_assert_cmpuint (g_list_model_get_n_items (memory), ==, 0);
      }
    }

  g_unlink (path);
}

static void
write_fixture (const char *dir,
               const char *name,
               const char *contents)
{
  g_autofree char *path = g_build_filename (dir, name, NULL);

  g_assert_true (g_file_set_contents (path, contents, -1, NULL));
}

static void
test_pmu (void)
{
  g_autofree char *dir = g_dir_make_tmp ("sysprof-pmu-XXXXXX", NULL);
  g_autofree char *events = g_build_filename (dir, "events", NULL);
  g_autofree char *format = g_build_filename (dir, "format", NULL);
  g_autoptr(GArray) cpus = NULL;
  g_autoptr(GError) error = NULL;
  struct perf_event_attr attr;
  const char *files[] = {
    "events/mem-loads",
    "events/mem-loads-aux",
    "format/event",
    "format/umask",
    "format/ldlat",
    "type",
    "cpus"
  };

  g_assert_cmpint (g_mkdir (events, 0700), ==, 0);
  g_assert_cmpint (g_mkdir (format, 0700), ==, 0);

  write_fixture (dir, "type", "12\n");
  write_fixture (dir, "cpus", "0-2,7\n");
  write_fixture (dir, "events/mem-loads", "event=0xcd,umask=0x101,ldlat=3\n");
  write_fixture (dir, "format/event", "config:0-7\n");
  write_fixture (dir, "format/umask", "config:8-15,40-47\n");
  write_fixture (dir, "format/ldlat", "config1:0-15\n");

  g_assert_true (_sysprof_memory_pmu_configure (dir, 30, &attr, &cpus, &error));
  g_assert_no_error (error);
  g_assert_cmpuint (attr.type, ==, 12);
  g_assert_cmpuint (attr.config, ==, 0x100000001cd);
  g_assert_cmpuint (attr.config1, ==, 30);
  g_assert_cmpuint (cpus->len, ==, 4);
  g_assert_cmpuint (g_array_index (cpus, guint, 3), ==, 7);

  g_clear_pointer (&cpus, g_array_unref);
  write_fixture (dir, "format/ldlat", "config1:64\n");
  g_assert_false (_sysprof_memory_pmu_configure (dir, 30, &attr, &cpus, &error));
  g_assert_nonnull (error);

  g_clear_error (&error);
  write_fixture (dir, "events/mem-loads-aux", "event=1\n");
  g_assert_false (_sysprof_memory_pmu_configure (dir, 30, &attr, &cpus, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);

  for (guint i = 0; i < G_N_ELEMENTS (files); i++)
    {
      g_autofree char *path = g_build_filename (dir, files[i], NULL);

      g_unlink (path);
    }

  g_rmdir (events);
  g_rmdir (format);
  g_rmdir (dir);
}

static void
test_attribute_transport (void)
{
  struct perf_event_attr attr = {
    .precise_ip = 2,
    .config1 = 30,
    .config2 = 7,
    .exclude_kernel = 1,
    .exclude_hv = 1,
    .read_format = SYSPROF_MEMORY_READ_FORMAT
  };
  g_autoptr(GVariant) options = _sysprof_perf_event_attr_to_variant (&attr);
  guint32 precise;
  guint64 value;
  gboolean excluded;

  g_assert_true (g_variant_lookup (options, "precise_ip", "u", &precise));
  g_assert_cmpuint (precise, ==, 2);

  g_assert_true (g_variant_lookup (options, "config1", "t", &value));
  g_assert_cmpuint (value, ==, 30);

  g_assert_true (g_variant_lookup (options, "config2", "t", &value));
  g_assert_cmpuint (value, ==, 7);

  g_assert_true (g_variant_lookup (options, "read_format", "t", &value));
  g_assert_cmpuint (value, ==, SYSPROF_MEMORY_READ_FORMAT);

  g_assert_true (g_variant_lookup (options, "exclude_kernel", "b", &excluded));
  g_assert_true (excluded);
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  dex_init ();

  g_test_add_data_func ("/memory-samples/native", GINT_TO_POINTER (FALSE), test_roundtrip);
  g_test_add_data_func ("/memory-samples/swapped", GINT_TO_POINTER (TRUE), test_roundtrip);
  g_test_add_data_func ("/memory-samples/cat", GINT_TO_POINTER (FALSE), test_cat);
  g_test_add_data_func ("/memory-samples/splice", GINT_TO_POINTER (TRUE), test_cat);
  g_test_add_data_func ("/memory-samples/cat-swapped", GINT_TO_POINTER (2), test_cat);
  g_test_add_func ("/memory-samples/perf-parser", test_perf_parser);
  g_test_add_func ("/memory-samples/perf-optional", test_perf_optional);
  g_test_add_func ("/memory-samples/invalid-capture", test_invalid_capture);
  g_test_add_func ("/memory-samples/pmu", test_pmu);
  g_test_add_func ("/memory-samples/attribute-transport", test_attribute_transport);

  return g_test_run ();
}
