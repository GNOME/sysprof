/* sysprof-capture.h
 *
 * Copyright © 2018 Christian Hergert <chergert@redhat.com>
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
 */

#ifndef SYSPROF_CAPTURE_H
#define SYSPROF_CAPTURE_H

#include <glib.h>

G_BEGIN_DECLS

#define SYSPROF_INSIDE

# include "sp-address.h"
# include "sp-clock.h"
# include "sp-error.h"
# include "sysprof-version.h"

# include "capture/sp-capture-reader.h"
# include "capture/sp-capture-writer.h"

#undef SYSPROF_INSIDE

G_END_DECLS

#endif /* SYSPROF_CAPTURE_H */
