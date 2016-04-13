/***
  This file is part of systemd.

  Copyright 2013 Lennart Poettering

  systemd is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  systemd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/

#ifndef SD_BUS_HELPER_H
#define SD_BUS_HELPER_H

#include <stdbool.h>
#include <systemd/sd-bus.h>

#define UID_INVALID ((uid_t)-1)

int bus_test_polkit (sd_bus_message  *call,
                     int              capability,
                     const char      *action,
                     const char     **details,
                     uid_t            good_user,
                     bool            *_challenge,
                     sd_bus_error    *e);

#endif /* SD_BUS_HELPER_H */
