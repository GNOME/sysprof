/* sysprof-categories-private.h
 *
 * Copyright 2023 Christian Hergert <chergert@redhat.com>
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

#include "sysprof-callgraph.h"

G_BEGIN_DECLS

typedef struct _SysprofCategories SysprofCategories;

SysprofCategories        *sysprof_categories_get_default (void) G_GNUC_CONST;
SysprofCategories        *sysprof_categories_new         (void);
void                      sysprof_categories_free        (SysprofCategories *categories);
SysprofCallgraphCategory  sysprof_categories_lookup      (SysprofCategories *categories,
                                                          const char        *binary_nick,
                                                          const char        *symbol);

G_END_DECLS
