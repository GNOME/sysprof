/*
 * Copyright 2024 Simon McVittie
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <sysprof.h>

#undef _NDEBUG
#include <assert.h>

int
main (G_GNUC_UNUSED int   argc,
      G_GNUC_UNUSED char *argv[])
{
  assert (sysprof_callgraph_flags_get_type () != 0);
  return 0;
}
