/* sp-capture-types.h
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
 *
 * This file is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This file is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib.h>

#include "sp-clock.h"

G_BEGIN_DECLS

#define SP_CAPTURE_MAGIC (GUINT32_TO_LE(0xFDCA975E))
#define SP_CAPTURE_ALIGN (sizeof(SpCaptureAddress))

#if defined(_MSC_VER)
# define SP_ALIGNED_BEGIN(_N) __declspec(align (_N))
# define SP_ALIGNED_END(_N)
#else
# define SP_ALIGNED_BEGIN(_N)
# define SP_ALIGNED_END(_N) __attribute__((aligned ((_N))))
#endif

#if GLIB_SIZEOF_VOID_P == 8
# define SP_CAPTURE_JITMAP_MARK    G_GUINT64_CONSTANT(0xE000000000000000)
# define SP_CAPTURE_ADDRESS_FORMAT "0x%016lx"
#elif GLIB_SIZEOF_VOID_P == 4
# define SP_CAPTURE_JITMAP_MARK    G_GUINT64_CONSTANT(0xE0000000)
# define SP_CAPTURE_ADDRESS_FORMAT "0x%016llx"
#else
#error Unknown GLIB_SIZEOF_VOID_P
#endif

#define SP_CAPTURE_CURRENT_TIME   (sp_clock_get_current_time())
#define SP_CAPTURE_COUNTER_INT64  0
#define SP_CAPTURE_COUNTER_DOUBLE 1

typedef struct _SpCaptureReader    SpCaptureReader;
typedef struct _SpCaptureWriter    SpCaptureWriter;
typedef struct _SpCaptureCursor    SpCaptureCursor;
typedef struct _SpCaptureCondition SpCaptureCondition;

typedef guint64 SpCaptureAddress;

typedef union
{
  gint64  v64;
  gdouble vdbl;
} SpCaptureCounterValue;

typedef enum
{
  SP_CAPTURE_FRAME_TIMESTAMP = 1,
  SP_CAPTURE_FRAME_SAMPLE    = 2,
  SP_CAPTURE_FRAME_MAP       = 3,
  SP_CAPTURE_FRAME_PROCESS   = 4,
  SP_CAPTURE_FRAME_FORK      = 5,
  SP_CAPTURE_FRAME_EXIT      = 6,
  SP_CAPTURE_FRAME_JITMAP    = 7,
  SP_CAPTURE_FRAME_CTRDEF    = 8,
  SP_CAPTURE_FRAME_CTRSET    = 9,
  SP_CAPTURE_FRAME_MARK      = 10,
} SpCaptureFrameType;

SP_ALIGNED_BEGIN(1)
typedef struct
{
  guint32 magic;
  guint32 version : 8;
  guint32 little_endian : 1;
  guint32 padding : 23;
  gchar   capture_time[64];
  gint64  time;
  gint64  end_time;
  gchar   suffix[168];
} SpCaptureFileHeader
SP_ALIGNED_END(1);

SP_ALIGNED_BEGIN(1)
typedef struct
{
  guint16 len;
  gint16  cpu;
  gint32  pid;
  gint64  time;
  guint32 type : 8;
  guint32 padding1 : 24;
  guint32 padding2;
  guint8  data[0];
} SpCaptureFrame
SP_ALIGNED_END(1);

SP_ALIGNED_BEGIN(1)
typedef struct
{
  SpCaptureFrame frame;
  guint64        start;
  guint64        end;
  guint64        offset;
  guint64        inode;
  gchar          filename[0];
} SpCaptureMap
SP_ALIGNED_END(1);

SP_ALIGNED_BEGIN(1)
typedef struct
{
  SpCaptureFrame frame;
  guint32        n_jitmaps;
  guint8         data[0];
} SpCaptureJitmap
SP_ALIGNED_END(1);

SP_ALIGNED_BEGIN(1)
typedef struct
{
  SpCaptureFrame frame;
  gchar          cmdline[0];
} SpCaptureProcess
SP_ALIGNED_END(1);

SP_ALIGNED_BEGIN(1)
typedef struct
{
  SpCaptureFrame   frame;
  guint32          n_addrs : 16;
  guint32          padding1 : 16;
  gint32           tid;
  SpCaptureAddress addrs[0];
} SpCaptureSample
SP_ALIGNED_END(1);

SP_ALIGNED_BEGIN(1)
typedef struct
{
  SpCaptureFrame frame;
  gint32         child_pid;
} SpCaptureFork
SP_ALIGNED_END(1);

SP_ALIGNED_BEGIN(1)
typedef struct
{
  SpCaptureFrame frame;
} SpCaptureExit
SP_ALIGNED_END(1);

SP_ALIGNED_BEGIN(1)
typedef struct
{
  SpCaptureFrame frame;
} SpCaptureTimestamp
SP_ALIGNED_END(1);

SP_ALIGNED_BEGIN(1)
typedef struct
{
  gchar                 category[32];
  gchar                 name[32];
  gchar                 description[52];
  guint32               id : 24;
  guint32               type : 8;
  SpCaptureCounterValue value;
} SpCaptureCounter
SP_ALIGNED_END(1);

SP_ALIGNED_BEGIN(1)
typedef struct
{
  SpCaptureFrame   frame;
  guint32          n_counters : 16;
  guint32          padding1 : 16;
  guint32          padding2;
  SpCaptureCounter counters[0];
} SpCaptureFrameCounterDefine
SP_ALIGNED_END(1);

SP_ALIGNED_BEGIN(1)
typedef struct
{
  /*
   * 96 bytes might seem a bit odd, but the counter frame header is 32
   * bytes.  So this makes a nice 2-cacheline aligned size which is
   * useful when the number of counters is rather small.
   */
  guint32               ids[8];
  SpCaptureCounterValue values[8];
} SpCaptureCounterValues
SP_ALIGNED_END(1);

SP_ALIGNED_BEGIN(1)
typedef struct
{
  SpCaptureFrame         frame;
  guint32                n_values : 16;
  guint32                padding1 : 16;
  guint32                padding2;
  SpCaptureCounterValues values[0];
} SpCaptureFrameCounterSet
SP_ALIGNED_END(1);

SP_ALIGNED_BEGIN(1)
typedef struct
{
  SpCaptureFrame frame;
  gint64         duration;
  gchar          group[24];
  gchar          name[40];
  gchar          message[0];
} SpCaptureMark
SP_ALIGNED_END(1);

G_STATIC_ASSERT (sizeof (SpCaptureFileHeader) == 256);
G_STATIC_ASSERT (sizeof (SpCaptureFrame) == 24);
G_STATIC_ASSERT (sizeof (SpCaptureMap) == 56);
G_STATIC_ASSERT (sizeof (SpCaptureJitmap) == 28);
G_STATIC_ASSERT (sizeof (SpCaptureProcess) == 24);
G_STATIC_ASSERT (sizeof (SpCaptureSample) == 32);
G_STATIC_ASSERT (sizeof (SpCaptureFork) == 28);
G_STATIC_ASSERT (sizeof (SpCaptureExit) == 24);
G_STATIC_ASSERT (sizeof (SpCaptureTimestamp) == 24);
G_STATIC_ASSERT (sizeof (SpCaptureCounter) == 128);
G_STATIC_ASSERT (sizeof (SpCaptureCounterValues) == 96);
G_STATIC_ASSERT (sizeof (SpCaptureFrameCounterDefine) == 32);
G_STATIC_ASSERT (sizeof (SpCaptureFrameCounterSet) == 32);
G_STATIC_ASSERT (sizeof (SpCaptureMark) == 96);

static inline gint
sp_capture_address_compare (SpCaptureAddress a,
                            SpCaptureAddress b)
{
  if (a < b)
    return -1;
  if (a > b)
    return 1;
  else
    return 0;
}

G_END_DECLS
