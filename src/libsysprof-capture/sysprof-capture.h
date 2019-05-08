/* sysprof-capture.h
 *
 * Copyright 2018-2019 Christian Hergert <chergert@redhat.com>
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

G_BEGIN_DECLS

#define SYSPROF_CAPTURE_INSIDE

# include "sp-address.h"
# include "sp-capture-condition.h"
# include "sp-capture-cursor.h"
# include "sp-capture-reader.h"
# include "sp-capture-writer.h"
# include "sp-clock.h"
# include "sysprof-version.h"
# include "sysprof-version-macros.h"

#undef SYSPROF_CAPTURE_INSIDE

G_END_DECLS
