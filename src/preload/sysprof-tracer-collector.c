/* sysprof-tracer.c
 *
 * Copyright 2022 Christian Hergert <chergert@redhat.com>
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

#include <glib.h>
#include <sysprof-capture.h>
#include <unistd.h>

#include "backtrace-helper.h"

#include "gconstructor.h"

#ifdef __GNUC__
# define GNUC_CHECK_VERSION(major, minor) \
    ((__GNUC__ > (major)) || \
     ((__GNUC__ == (major)) && \
      (__GNUC_MINOR__ >= (minor))))
#else
# define GNUC_CHECK_VERSION(major, minor) 0
#endif

#if defined (G_HAS_CONSTRUCTORS)
# ifdef G_DEFINE_CONSTRUCTOR_NEEDS_PRAGMA
#  pragma G_DEFINE_CONSTRUCTOR_PRAGMA_ARGS(collector_init_ctor)
# endif
G_DEFINE_CONSTRUCTOR(collector_init_ctor)
#else
# error Your platform/compiler is missing constructor support
#endif

#ifndef __APPLE__
# define profile_func_enter __cyg_profile_func_enter
# define profile_func_exit __cyg_profile_func_exit
#endif

static void
collector_init_ctor (void)
{
  backtrace_init ();
  sysprof_collector_init ();
}

/*
 * What we would really want to do is to have a new frame type for enter/exit
 * tracing so that we can only push/pop the new address to the sample. Then
 * when decoding it can recreate stack traces if necessary. But for now, we
 * can emulate that by just adding a sample when we enter a function and
 * leave a function. The rest could be done in post-processing.
 */

#if GNUC_CHECK_VERSION(3,0)
__attribute__((no_instrument_function))
#endif
void
profile_func_enter (void *func,
                    void *call_site)
{
  sysprof_collector_trace (backtrace_func, NULL, TRUE);
}

#if GNUC_CHECK_VERSION(3,0)
__attribute__((no_instrument_function))
#endif
void
profile_func_exit (void *func,
                   void *call_site)
{
  sysprof_collector_trace (backtrace_func, NULL, FALSE);
}
