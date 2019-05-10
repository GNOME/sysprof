#pragma once

#include "config.h"

#ifndef HAVE_POLKIT_AUTOPTR
# include <polkit/polkit.h>

  G_DEFINE_AUTOPTR_CLEANUP_FUNC (PolkitAuthority, g_object_unref)
  G_DEFINE_AUTOPTR_CLEANUP_FUNC (PolkitAuthorizationResult, g_object_unref)
  G_DEFINE_AUTOPTR_CLEANUP_FUNC (PolkitSubject, g_object_unref)
#endif

