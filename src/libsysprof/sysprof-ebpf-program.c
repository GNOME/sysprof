/*
 * sysprof-ebpf-program.c
 *
 * Copyright 2025 Christian Hergert <chergert@redhat.com>
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

#include "sysprof-ebpf-program-private.h"

enum {
  PROP_0,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofEbpfProgram, sysprof_ebpf_program, SYSPROF_TYPE_INSTRUMENT)

static GParamSpec *properties[N_PROPS];

static void
sysprof_ebpf_program_finalize (GObject *object)
{
  SysprofEbpfProgram *self = (SysprofEbpfProgram *)object;

  G_OBJECT_CLASS (sysprof_ebpf_program_parent_class)->finalize (object);
}

static void
sysprof_ebpf_program_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  SysprofEbpfProgram *self = SYSPROF_EBPF_PROGRAM (object);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_ebpf_program_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  SysprofEbpfProgram *self = SYSPROF_EBPF_PROGRAM (object);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_ebpf_program_class_init (SysprofEbpfProgramClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_ebpf_program_finalize;
  object_class->get_property = sysprof_ebpf_program_get_property;
  object_class->set_property = sysprof_ebpf_program_set_property;
}

static void
sysprof_ebpf_program_init (SysprofEbpfProgram *self)
{
}
