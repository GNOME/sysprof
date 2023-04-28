/* sysprof-document-exit.c
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

#include "config.h"

#include "sysprof-document-frame-private.h"
#include "sysprof-document-exit.h"

struct _SysprofDocumentExit
{
  SysprofDocumentFrame parent_instance;
};

struct _SysprofDocumentExitClass
{
  SysprofDocumentFrameClass parent_class;
};

G_DEFINE_FINAL_TYPE (SysprofDocumentExit, sysprof_document_exit, SYSPROF_TYPE_DOCUMENT_FRAME)

static void
sysprof_document_exit_class_init (SysprofDocumentExitClass *klass)
{
}

static void
sysprof_document_exit_init (SysprofDocumentExit *self)
{
}
