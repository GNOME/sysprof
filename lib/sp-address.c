/* sp-address.c
 *
 * Copyright © 2016 Christian Hergert <chergert@redhat.com>
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
 */

#include <linux/perf_event.h>

#include "sp-address.h"

gboolean
sp_address_is_context_switch (SpAddress         address,
                              SpAddressContext *context)
{
  SpAddressContext dummy;

  if (context == NULL)
    context = &dummy;

  switch (address)
    {
    case PERF_CONTEXT_HV:
      *context = SP_ADDRESS_CONTEXT_HYPERVISOR;
      return TRUE;

    case PERF_CONTEXT_KERNEL:
      *context = SP_ADDRESS_CONTEXT_KERNEL;
      return TRUE;

    case PERF_CONTEXT_USER:
      *context = SP_ADDRESS_CONTEXT_USER;
      return TRUE;

    case PERF_CONTEXT_GUEST:
      *context = SP_ADDRESS_CONTEXT_GUEST;
      return TRUE;

    case PERF_CONTEXT_GUEST_KERNEL:
      *context = SP_ADDRESS_CONTEXT_GUEST_KERNEL;
      return TRUE;

    case PERF_CONTEXT_GUEST_USER:
      *context = SP_ADDRESS_CONTEXT_GUEST_USER;
      return TRUE;

    default:
      *context = SP_ADDRESS_CONTEXT_NONE;
      return FALSE;
    }
}

const gchar *
sp_address_context_to_string (SpAddressContext context)
{
  switch (context)
    {
      case SP_ADDRESS_CONTEXT_HYPERVISOR:
        return "- - hypervisor - -";

      case SP_ADDRESS_CONTEXT_KERNEL:
        return "- - kernel - -";

      case SP_ADDRESS_CONTEXT_USER:
        return "- - user - -";

      case SP_ADDRESS_CONTEXT_GUEST:
        return "- - guest - -";

      case SP_ADDRESS_CONTEXT_GUEST_KERNEL:
        return "- - guest kernel - -";

      case SP_ADDRESS_CONTEXT_GUEST_USER:
        return "- - guest user - -";

      case SP_ADDRESS_CONTEXT_NONE:
      default:
        return "- - unknown - -";
    }
}
