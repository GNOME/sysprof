/* sp-kallsyms.h
 *
 * Copyright © 2018 Christian Hergert <chergert@redhat.com>
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
 */

#ifndef SP_KALLSYMS_H
#define SP_KALLSYMS_H

#include <gio/gio.h>

#include "sp-kallsyms.h"

G_BEGIN_DECLS

typedef struct _SpKallsyms SpKallsyms;

SpKallsyms *sp_kallsyms_new  (const gchar  *path);
gboolean    sp_kallsyms_next (SpKallsyms   *self,
                              const gchar **name,
                              guint64      *address,
                              guint8       *type);
void        sp_kallsyms_free (SpKallsyms   *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SpKallsyms, sp_kallsyms_free)

G_END_DECLS

#endif /* SP_KALLSYMS_H */
