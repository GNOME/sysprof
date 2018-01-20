/* sp-error.h
 *
 * Copyright © 2016 Christian Hergert <chergert@redhat.com>
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

#ifndef SP_ERROR_H
#define SP_ERROR_H

#include <glib.h>

G_BEGIN_DECLS

#define SP_ERROR (sp_error_quark())

GQuark sp_error_quark (void);

G_END_DECLS

#endif /* SP_ERROR_H */
