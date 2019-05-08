/* sp-version-macros.h
 *
 * Copyright 2018-2019 Christian Hergert <chergert@redhat.com>
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

#include "sysprof-version.h"

#ifndef _SYSPROF_EXTERN
#define _SYSPROF_EXTERN extern
#endif

#ifdef SYSPROF_DISABLE_DEPRECATION_WARNINGS
#define SYSPROF_DEPRECATED _SYSPROF_EXTERN
#define SYSPROF_DEPRECATED_FOR(f) _SYSPROF_EXTERN
#define SYSPROF_UNAVAILABLE(maj,min) _SYSPROF_EXTERN
#else
#define SYSPROF_DEPRECATED G_DEPRECATED _SYSPROF_EXTERN
#define SYSPROF_DEPRECATED_FOR(f) G_DEPRECATED_FOR(f) _SYSPROF_EXTERN
#define SYSPROF_UNAVAILABLE(maj,min) G_UNAVAILABLE(maj,min) _SYSPROF_EXTERN
#endif

#define SYSPROF_VERSION_3_34 (G_ENCODE_VERSION (3, 34))

#if (SYSPROF_MINOR_VERSION == 99)
# define SYSPROF_VERSION_CUR_STABLE (G_ENCODE_VERSION (SYSPROF_MAJOR_VERSION + 1, 0))
#elif (SYSPROF_MINOR_VERSION % 2)
# define SYSPROF_VERSION_CUR_STABLE (G_ENCODE_VERSION (SYSPROF_MAJOR_VERSION, SYSPROF_MINOR_VERSION + 1))
#else
# define SYSPROF_VERSION_CUR_STABLE (G_ENCODE_VERSION (SYSPROF_MAJOR_VERSION, SYSPROF_MINOR_VERSION))
#endif

#if (SYSPROF_MINOR_VERSION == 99)
# define SYSPROF_VERSION_PREV_STABLE (G_ENCODE_VERSION (SYSPROF_MAJOR_VERSION + 1, 0))
#elif (SYSPROF_MINOR_VERSION % 2)
# define SYSPROF_VERSION_PREV_STABLE (G_ENCODE_VERSION (SYSPROF_MAJOR_VERSION, SYSPROF_MINOR_VERSION - 1))
#else
# define SYSPROF_VERSION_PREV_STABLE (G_ENCODE_VERSION (SYSPROF_MAJOR_VERSION, SYSPROF_MINOR_VERSION - 2))
#endif

/**
 * SYSPROF_VERSION_MIN_REQUIRED:
 *
 * A macro that should be defined by the user prior to including
 * the ide.h header.
 *
 * The definition should be one of the predefined IDE version
 * macros: %SYSPROF_VERSION_3_34, ...
 *
 * This macro defines the lower bound for the Builder API to use.
 *
 * If a function has been deprecated in a newer version of Builder,
 * it is possible to use this symbol to avoid the compiler warnings
 * without disabling warning for every deprecated function.
 *
 * Since: 3.34
 */
#ifndef SYSPROF_VERSION_MIN_REQUIRED
# define SYSPROF_VERSION_MIN_REQUIRED (SYSPROF_VERSION_CUR_STABLE)
#endif

/**
 * SYSPROF_VERSION_MAX_ALLOWED:
 *
 * A macro that should be defined by the user prior to including
 * the ide.h header.

 * The definition should be one of the predefined Builder version
 * macros: %SYSPROF_VERSION_1_0, %SYSPROF_VERSION_1_2,...
 *
 * This macro defines the upper bound for the IDE API to use.
 *
 * If a function has been introduced in a newer version of Builder,
 * it is possible to use this symbol to get compiler warnings when
 * trying to use that function.
 *
 * Since: 3.34
 */
#ifndef SYSPROF_VERSION_MAX_ALLOWED
# if SYSPROF_VERSION_MIN_REQUIRED > SYSPROF_VERSION_PREV_STABLE
#  define SYSPROF_VERSION_MAX_ALLOWED (SYSPROF_VERSION_MIN_REQUIRED)
# else
#  define SYSPROF_VERSION_MAX_ALLOWED (SYSPROF_VERSION_CUR_STABLE)
# endif
#endif

#if SYSPROF_VERSION_MAX_ALLOWED < SYSPROF_VERSION_MIN_REQUIRED
#error "SYSPROF_VERSION_MAX_ALLOWED must be >= SYSPROF_VERSION_MIN_REQUIRED"
#endif
#if SYSPROF_VERSION_MIN_REQUIRED < SYSPROF_VERSION_3_34
#error "SYSPROF_VERSION_MIN_REQUIRED must be >= SYSPROF_VERSION_3_34"
#endif

#define SYSPROF_AVAILABLE_IN_ALL                   _SYSPROF_EXTERN

#if SYSPROF_VERSION_MIN_REQUIRED >= SYSPROF_VERSION_3_34
# define SYSPROF_DEPRECATED_IN_3_34                SYSPROF_DEPRECATED
# define SYSPROF_DEPRECATED_IN_3_34_FOR(f)         SYSPROF_DEPRECATED_FOR(f)
#else
# define SYSPROF_DEPRECATED_IN_3_34                _SYSPROF_EXTERN
# define SYSPROF_DEPRECATED_IN_3_34_FOR(f)         _SYSPROF_EXTERN
#endif

#if SYSPROF_VERSION_MAX_ALLOWED < SYSPROF_VERSION_3_34
# define SYSPROF_AVAILABLE_IN_3_34                 SYSPROF_UNAVAILABLE(3, 34)
#else
# define SYSPROF_AVAILABLE_IN_3_34                 _SYSPROF_EXTERN
#endif
