/* sysprof-document-fork.c
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
#include "sysprof-document-fork.h"

struct _SysprofDocumentFork
{
  SysprofDocumentFrame parent_instance;
};

struct _SysprofDocumentForkClass
{
  SysprofDocumentFrameClass parent_class;
};

enum {
  PROP_0,
  PROP_CHILD_PID,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofDocumentFork, sysprof_document_fork, SYSPROF_TYPE_DOCUMENT_FRAME)

static GParamSpec *properties [N_PROPS];

static void
sysprof_document_fork_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  SysprofDocumentFork *self = SYSPROF_DOCUMENT_FORK (object);

  switch (prop_id)
    {
    case PROP_CHILD_PID:
      g_value_set_int (value, sysprof_document_fork_get_child_pid (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_document_fork_class_init (SysprofDocumentForkClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = sysprof_document_fork_get_property;

  properties [PROP_CHILD_PID] =
    g_param_spec_int ("child-pid", NULL, NULL,
                      G_MININT, G_MAXINT, 0,
                      (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_document_fork_init (SysprofDocumentFork *self)
{
}

int
sysprof_document_fork_get_child_pid (SysprofDocumentFork *self)
{
  const SysprofCaptureFork *fork;

  g_return_val_if_fail (SYSPROF_IS_DOCUMENT_FORK (self), 0);

  fork = SYSPROF_DOCUMENT_FRAME_GET (self, SysprofCaptureFork);

  return SYSPROF_DOCUMENT_FRAME_INT32 (self, fork->child_pid);
}
