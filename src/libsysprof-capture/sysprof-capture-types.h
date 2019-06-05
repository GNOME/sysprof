/* sysprof-capture-types.h
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * Subject to the terms and conditions of this license, each copyright holder
 * and contributor hereby grants to those receiving rights under this license
 * a perpetual, worldwide, non-exclusive, no-charge, royalty-free,
 * irrevocable (except for failure to satisfy the conditions of this license)
 * patent license to make, have made, use, offer to sell, sell, import, and
 * otherwise transfer this software, where such license applies only to those
 * patent claims, already acquired or hereafter acquired, licensable by such
 * copyright holder or contributor that are necessarily infringed by:
 *
 * (a) their Contribution(s) (the licensed copyrights of copyright holders
 *     and non-copyrightable additions of contributors, in source or binary
 *     form) alone; or
 *
 * (b) combination of their Contribution(s) with the work of authorship to
 *     which such Contribution(s) was added by such copyright holder or
 *     contributor, if, at the time the Contribution is added, such addition
 *     causes such combination to be necessarily infringed. The patent license
 *     shall not apply to any other combinations which include the
 *     Contribution.
 *
 * Except as expressly stated above, no rights or licenses from any copyright
 * holder or contributor is granted under this license, whether expressly, by
 * implication, estoppel or otherwise.
 *
 * DISCLAIMER
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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

typedef struct
{
  /*
   * The number of frames indexed by SysprofCaptureFrameType
   */
  gsize frame_count[16];

  /*
   * Padding for future expansion.
   */
  gsize padding[48];
} SysprofCaptureStat;

typedef union
{
  gint64  v64;
  gdouble vdbl;
} SysprofCaptureCounterValue;

typedef enum
{
  SYSPROF_CAPTURE_FRAME_TIMESTAMP  = 1,
  SYSPROF_CAPTURE_FRAME_SAMPLE     = 2,
  SYSPROF_CAPTURE_FRAME_MAP        = 3,
  SYSPROF_CAPTURE_FRAME_PROCESS    = 4,
  SYSPROF_CAPTURE_FRAME_FORK       = 5,
  SYSPROF_CAPTURE_FRAME_EXIT       = 6,
  SYSPROF_CAPTURE_FRAME_JITMAP     = 7,
  SYSPROF_CAPTURE_FRAME_CTRDEF     = 8,
  SYSPROF_CAPTURE_FRAME_CTRSET     = 9,
  SYSPROF_CAPTURE_FRAME_MARK       = 10,
  SYSPROF_CAPTURE_FRAME_METADATA   = 11,
  SYSPROF_CAPTURE_FRAME_LOG        = 12,
  SYSPROF_CAPTURE_FRAME_FILE_CHUNK = 13,
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
} SysprofCaptureCounterDefine
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
} SysprofCaptureCounterSet
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

SYSPROF_ALIGNED_BEGIN(1)
typedef struct
{
  SysprofCaptureFrame frame;
  gchar               id[40];
  gchar               metadata[0];
} SysprofCaptureMetadata
SYSPROF_ALIGNED_END(1);

SYSPROF_ALIGNED_BEGIN(1)
typedef struct
{
  SysprofCaptureFrame frame;
  guint32             severity : 16;
  guint32             padding1 : 16;
  guint32             padding2 : 32;
  gchar               domain[32];
  gchar               message[0];
} SysprofCaptureLog
SYSPROF_ALIGNED_END(1);

SYSPROF_ALIGNED_BEGIN(1)
typedef struct
{
  SysprofCaptureFrame frame;
  guint32             is_last : 1;
  guint32             padding1 : 15;
  guint32             len : 16;
  gchar               path[256];
  guint8              data[0];
} SysprofCaptureFileChunk
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
G_STATIC_ASSERT (sizeof (SysprofCaptureCounterDefine) == 32);
G_STATIC_ASSERT (sizeof (SysprofCaptureCounterSet) == 32);
G_STATIC_ASSERT (sizeof (SysprofCaptureMark) == 96);
G_STATIC_ASSERT (sizeof (SysprofCaptureMetadata) == 64);
G_STATIC_ASSERT (sizeof (SysprofCaptureLog) == 64);
G_STATIC_ASSERT (sizeof (SysprofCaptureFileChunk) == 284);

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
