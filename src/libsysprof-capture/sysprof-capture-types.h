/* sysprof-capture-types.h
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

#include "sysprof-clock.h"

G_BEGIN_DECLS

#define SYSPROF_CAPTURE_MAGIC (GUINT32_TO_LE(0xFDCA975E))
#define SYSPROF_CAPTURE_ALIGN (sizeof(SysprofCaptureAddress))

#if defined(_MSC_VER)
# define SYSPROF_ALIGNED_BEGIN(_N) __declspec(align (_N))
# define SYSPROF_ALIGNED_END(_N)
#else
# define SYSPROF_ALIGNED_BEGIN(_N)
# define SYSPROF_ALIGNED_END(_N) __attribute__((aligned ((_N))))
#endif

#if GLIB_SIZEOF_VOID_P == 8
# define SYSPROF_CAPTURE_JITMAP_MARK    G_GUINT64_CONSTANT(0xE000000000000000)
# define SYSPROF_CAPTURE_ADDRESS_FORMAT "0x%016lx"
#elif GLIB_SIZEOF_VOID_P == 4
# define SYSPROF_CAPTURE_JITMAP_MARK    G_GUINT64_CONSTANT(0xE0000000)
# define SYSPROF_CAPTURE_ADDRESS_FORMAT "0x%016llx"
#else
#error Unknown GLIB_SIZEOF_VOID_P
#endif

#define SYSPROF_CAPTURE_CURRENT_TIME   (sysprof_clock_get_current_time())
#define SYSPROF_CAPTURE_COUNTER_INT64  0
#define SYSPROF_CAPTURE_COUNTER_DOUBLE 1

typedef struct _SysprofCaptureReader    SysprofCaptureReader;
typedef struct _SysprofCaptureWriter    SysprofCaptureWriter;
typedef struct _SysprofCaptureCursor    SysprofCaptureCursor;
typedef struct _SysprofCaptureCondition SysprofCaptureCondition;

typedef guint64 SysprofCaptureAddress;

typedef union
{
  gint64  v64;
  gdouble vdbl;
} SysprofCaptureCounterValue;

typedef enum
{
  SYSPROF_CAPTURE_FRAME_TIMESTAMP = 1,
  SYSPROF_CAPTURE_FRAME_SAMPLE    = 2,
  SYSPROF_CAPTURE_FRAME_MAP       = 3,
  SYSPROF_CAPTURE_FRAME_PROCESS   = 4,
  SYSPROF_CAPTURE_FRAME_FORK      = 5,
  SYSPROF_CAPTURE_FRAME_EXIT      = 6,
  SYSPROF_CAPTURE_FRAME_JITMAP    = 7,
  SYSPROF_CAPTURE_FRAME_CTRDEF    = 8,
  SYSPROF_CAPTURE_FRAME_CTRSET    = 9,
  SYSPROF_CAPTURE_FRAME_MARK      = 10,
} SysprofCaptureFrameType;

SYSPROF_ALIGNED_BEGIN(1)
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
} SysprofCaptureFileHeader
SYSPROF_ALIGNED_END(1);

SYSPROF_ALIGNED_BEGIN(1)
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
} SysprofCaptureFrame
SYSPROF_ALIGNED_END(1);

SYSPROF_ALIGNED_BEGIN(1)
typedef struct
{
  SysprofCaptureFrame frame;
  guint64             start;
  guint64             end;
  guint64             offset;
  guint64             inode;
  gchar               filename[0];
} SysprofCaptureMap
SYSPROF_ALIGNED_END(1);

SYSPROF_ALIGNED_BEGIN(1)
typedef struct
{
  SysprofCaptureFrame frame;
  guint32             n_jitmaps;
  guint8              data[0];
} SysprofCaptureJitmap
SYSPROF_ALIGNED_END(1);

SYSPROF_ALIGNED_BEGIN(1)
typedef struct
{
  SysprofCaptureFrame frame;
  gchar               cmdline[0];
} SysprofCaptureProcess
SYSPROF_ALIGNED_END(1);

SYSPROF_ALIGNED_BEGIN(1)
typedef struct
{
  SysprofCaptureFrame   frame;
  guint32               n_addrs : 16;
  guint32               padding1 : 16;
  gint32                tid;
  SysprofCaptureAddress addrs[0];
} SysprofCaptureSample
SYSPROF_ALIGNED_END(1);

SYSPROF_ALIGNED_BEGIN(1)
typedef struct
{
  SysprofCaptureFrame frame;
  gint32              child_pid;
} SysprofCaptureFork
SYSPROF_ALIGNED_END(1);

SYSPROF_ALIGNED_BEGIN(1)
typedef struct
{
  SysprofCaptureFrame frame;
} SysprofCaptureExit
SYSPROF_ALIGNED_END(1);

SYSPROF_ALIGNED_BEGIN(1)
typedef struct
{
  SysprofCaptureFrame frame;
} SysprofCaptureTimestamp
SYSPROF_ALIGNED_END(1);

SYSPROF_ALIGNED_BEGIN(1)
typedef struct
{
  gchar                      category[32];
  gchar                      name[32];
  gchar                      description[52];
  guint32                    id : 24;
  guint32                    type : 8;
  SysprofCaptureCounterValue value;
} SysprofCaptureCounter
SYSPROF_ALIGNED_END(1);

SYSPROF_ALIGNED_BEGIN(1)
typedef struct
{
  SysprofCaptureFrame   frame;
  guint32               n_counters : 16;
  guint32               padding1 : 16;
  guint32               padding2;
  SysprofCaptureCounter counters[0];
} SysprofCaptureFrameCounterDefine
SYSPROF_ALIGNED_END(1);

SYSPROF_ALIGNED_BEGIN(1)
typedef struct
{
  /*
   * 96 bytes might seem a bit odd, but the counter frame header is 32
   * bytes.  So this makes a nice 2-cacheline aligned size which is
   * useful when the number of counters is rather small.
   */
  guint32                    ids[8];
  SysprofCaptureCounterValue values[8];
} SysprofCaptureCounterValues
SYSPROF_ALIGNED_END(1);

SYSPROF_ALIGNED_BEGIN(1)
typedef struct
{
  SysprofCaptureFrame         frame;
  guint32                     n_values : 16;
  guint32                     padding1 : 16;
  guint32                     padding2;
  SysprofCaptureCounterValues values[0];
} SysprofCaptureFrameCounterSet
SYSPROF_ALIGNED_END(1);

SYSPROF_ALIGNED_BEGIN(1)
typedef struct
{
  SysprofCaptureFrame frame;
  gint64              duration;
  gchar               group[24];
  gchar               name[40];
  gchar               message[0];
} SysprofCaptureMark
SYSPROF_ALIGNED_END(1);

G_STATIC_ASSERT (sizeof (SysprofCaptureFileHeader) == 256);
G_STATIC_ASSERT (sizeof (SysprofCaptureFrame) == 24);
G_STATIC_ASSERT (sizeof (SysprofCaptureMap) == 56);
G_STATIC_ASSERT (sizeof (SysprofCaptureJitmap) == 28);
G_STATIC_ASSERT (sizeof (SysprofCaptureProcess) == 24);
G_STATIC_ASSERT (sizeof (SysprofCaptureSample) == 32);
G_STATIC_ASSERT (sizeof (SysprofCaptureFork) == 28);
G_STATIC_ASSERT (sizeof (SysprofCaptureExit) == 24);
G_STATIC_ASSERT (sizeof (SysprofCaptureTimestamp) == 24);
G_STATIC_ASSERT (sizeof (SysprofCaptureCounter) == 128);
G_STATIC_ASSERT (sizeof (SysprofCaptureCounterValues) == 96);
G_STATIC_ASSERT (sizeof (SysprofCaptureFrameCounterDefine) == 32);
G_STATIC_ASSERT (sizeof (SysprofCaptureFrameCounterSet) == 32);
G_STATIC_ASSERT (sizeof (SysprofCaptureMark) == 96);

static inline gint
sysprof_capture_address_compare (SysprofCaptureAddress a,
                            SysprofCaptureAddress b)
{
  if (a < b)
    return -1;
  if (a > b)
    return 1;
  else
    return 0;
}

G_END_DECLS
