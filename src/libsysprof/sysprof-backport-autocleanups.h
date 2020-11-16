#pragma once

#include "config.h"

#include <glib.h>

#if HAVE_POLKIT
# ifndef HAVE_POLKIT_AUTOPTR
#  include <polkit/polkit.h>

   G_DEFINE_AUTOPTR_CLEANUP_FUNC (PolkitAuthority, g_object_unref)
   G_DEFINE_AUTOPTR_CLEANUP_FUNC (PolkitAuthorizationResult, g_object_unref)
   G_DEFINE_AUTOPTR_CLEANUP_FUNC (PolkitSubject, g_object_unref)
   G_DEFINE_AUTOPTR_CLEANUP_FUNC (PolkitDetails, g_object_unref)
# endif
#endif

#if !GLIB_CHECK_VERSION(2, 56, 0)
# define g_clear_handle_id(ptr, clear_func) \
    G_STMT_START {                          \
      guint __ptr = *(ptr);                 \
      *(ptr) = 0;                           \
      if (__ptr != 0)                       \
        clear_func (__ptr);                 \
    } G_STMT_END
#endif
