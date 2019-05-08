/* sysprof-address.c
 *
 * Copyright 2016-2019 Christian Hergert <chergert@redhat.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
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

#ifdef __linux__
# include <linux/perf_event.h>
#else
# include "sysprof-address-fallback.h"
#endif

#include "sysprof-address.h"

gboolean
sysprof_address_is_context_switch (SysprofAddress         address,
                              SysprofAddressContext *context)
{
  SysprofAddressContext dummy;

  if (context == NULL)
    context = &dummy;

  switch (address)
    {
    case PERF_CONTEXT_HV:
      *context = SYSPROF_ADDRESS_CONTEXT_HYPERVISOR;
      return TRUE;

    case PERF_CONTEXT_KERNEL:
      *context = SYSPROF_ADDRESS_CONTEXT_KERNEL;
      return TRUE;

    case PERF_CONTEXT_USER:
      *context = SYSPROF_ADDRESS_CONTEXT_USER;
      return TRUE;

    case PERF_CONTEXT_GUEST:
      *context = SYSPROF_ADDRESS_CONTEXT_GUEST;
      return TRUE;

    case PERF_CONTEXT_GUEST_KERNEL:
      *context = SYSPROF_ADDRESS_CONTEXT_GUEST_KERNEL;
      return TRUE;

    case PERF_CONTEXT_GUEST_USER:
      *context = SYSPROF_ADDRESS_CONTEXT_GUEST_USER;
      return TRUE;

    default:
      *context = SYSPROF_ADDRESS_CONTEXT_NONE;
      return FALSE;
    }
}

const gchar *
sysprof_address_context_to_string (SysprofAddressContext context)
{
  switch (context)
    {
      case SYSPROF_ADDRESS_CONTEXT_HYPERVISOR:
        return "- - hypervisor - -";

      case SYSPROF_ADDRESS_CONTEXT_KERNEL:
        return "- - kernel - -";

      case SYSPROF_ADDRESS_CONTEXT_USER:
        return "- - user - -";

      case SYSPROF_ADDRESS_CONTEXT_GUEST:
        return "- - guest - -";

      case SYSPROF_ADDRESS_CONTEXT_GUEST_KERNEL:
        return "- - guest kernel - -";

      case SYSPROF_ADDRESS_CONTEXT_GUEST_USER:
        return "- - guest user - -";

      case SYSPROF_ADDRESS_CONTEXT_NONE:
      default:
        return "- - unknown - -";
    }
}
