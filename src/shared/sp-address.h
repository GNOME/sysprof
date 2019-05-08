/* sp-address.h
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

G_BEGIN_DECLS

typedef guint64 SpAddress;

G_STATIC_ASSERT (sizeof (SpAddress) >= sizeof (gpointer));

typedef enum
{
  SP_ADDRESS_CONTEXT_NONE = 0,
  SP_ADDRESS_CONTEXT_HYPERVISOR,
  SP_ADDRESS_CONTEXT_KERNEL,
  SP_ADDRESS_CONTEXT_USER,
  SP_ADDRESS_CONTEXT_GUEST,
  SP_ADDRESS_CONTEXT_GUEST_KERNEL,
  SP_ADDRESS_CONTEXT_GUEST_USER,
} SpAddressContext;

gboolean     sp_address_is_context_switch (SpAddress         address,
                                           SpAddressContext *context);
const gchar *sp_address_context_to_string (SpAddressContext  context);

static inline gint
sp_address_compare (SpAddress a,
                    SpAddress b)
{
  if (a < b)
    return -1;
  else if (a == b)
    return 0;
  else
    return 1;
}

G_END_DECLS
