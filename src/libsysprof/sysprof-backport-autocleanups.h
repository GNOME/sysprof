#pragma once

#include "config.h"

#include <glib.h>

#ifndef HAVE_POLKIT_AUTOPTR
# include <polkit/polkit.h>

  G_DEFINE_AUTOPTR_CLEANUP_FUNC (PolkitAuthority, g_object_unref)
  G_DEFINE_AUTOPTR_CLEANUP_FUNC (PolkitAuthorizationResult, g_object_unref)
  G_DEFINE_AUTOPTR_CLEANUP_FUNC (PolkitSubject, g_object_unref)
#endif

#if !GLIB_CHECK_VERSION(2, 56, 0)
  static inline void
  g_clear_handle_id (guint *ptr,
                     void (*clear_func) (guint handle_id))
  {
    guint id = *ptr;
    *ptr = 0;
    if (id)
      clear_func (handle_id);
  }
#endif
