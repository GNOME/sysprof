#define _GNU_SOURCE

#include "config.h"

#include <dlfcn.h>
#ifdef HAVE_EXECINFO_H
# include <execinfo.h>
#endif
#ifdef ENABLE_LIBUNWIND
# include <libunwind.h>
#endif
#include <sched.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sysprof-capture.h>
#include <unistd.h>

#include "gconstructor.h"

typedef void *(* RealMalloc)        (size_t);
typedef void  (* RealFree)          (void *);
typedef void *(* RealCalloc)        (size_t, size_t);
typedef void *(* RealRealloc)       (void *, size_t);
typedef void *(* RealAlignedAlloc)  (size_t, size_t);
typedef int   (* RealPosixMemalign) (void **, size_t, size_t);
typedef void *(* RealMemalign)      (size_t, size_t);

typedef struct
{
  char buf[4092];
  int  off;
} ScratchAlloc;

static void  hook_memtable   (void);
static void *scratch_malloc  (size_t);
static void *scratch_realloc (void *, size_t);
static void *scratch_calloc  (size_t, size_t);
static void  scratch_free    (void *);

static int collector_ready;
static int hooked;
static ScratchAlloc scratch;
static RealCalloc real_calloc = scratch_calloc;
static RealFree real_free = scratch_free;
static RealMalloc real_malloc = scratch_malloc;
static RealRealloc real_realloc = scratch_realloc;
static RealAlignedAlloc real_aligned_alloc;
static RealPosixMemalign real_posix_memalign;
static RealMemalign real_memalign;

#if defined (G_HAS_CONSTRUCTORS)
# ifdef G_DEFINE_CONSTRUCTOR_NEEDS_PRAGMA
#  pragma G_DEFINE_CONSTRUCTOR_PRAGMA_ARGS(collector_init_ctor)
# endif
G_DEFINE_CONSTRUCTOR(collector_init_ctor)
#else
# error Your platform/compiler is missing constructor support
#endif

static void
collector_init_ctor (void)
{
  sysprof_collector_init ();
  g_atomic_int_set (&collector_ready, TRUE);
}

static guint
backtrace_func (SysprofCaptureAddress *addrs,
                guint                  n_addrs,
                gpointer               user_data)
{
#if defined(ENABLE_LIBUNWIND)
  unw_context_t uc;
  unw_cursor_t cursor;
  unw_word_t ip;

  unw_getcontext (&uc);
  unw_init_local (&cursor, &uc);

  /* Skip past caller frames */
  if (unw_step (&cursor) > 0 && unw_step (&cursor) > 0)
    {
      guint n = 0;

      /* Now walk the stack frames back */
      while (n < n_addrs && unw_step (&cursor) > 0)
        {
          unw_get_reg (&cursor, UNW_REG_IP, &ip);
          addrs[n++] = ip;
        }

      return n;
    }

  return 0;
#elif defined(HAVE_EXECINFO_H)
# if GLIB_SIZEOF_VOID_P == 8
  return backtrace ((void **)addrs, n_addrs);
# else /* GLIB_SIZEOF_VOID_P != 8 */
  void **stack = alloca (n_addrs * sizeof (gpointer));
  guint n = backtrace (stack, n_addrs);
  for (guint i = 0; i < n; i++)
    addrs[i] = GPOINTER_TO_SIZE (stack[i]);
  return n;
# endif /* GLIB_SIZEOF_VOID_P */
#else
  return 0;
#endif
}

static void *
scratch_malloc (size_t size)
{
  hook_memtable ();
  return real_malloc (size);
}

static void *
scratch_realloc (void   *ptr,
                 size_t  size)
{
  hook_memtable ();
  return real_realloc (ptr, size);
}

static void *
scratch_calloc (size_t nmemb,
                size_t size)
{
  void *ret;

  /* re-entrant, but forces early hook in case calloc is
   * called before any of our other hooks.
   */
  if (!hooked)
    hook_memtable ();

  size *= nmemb;
  ret = &scratch.buf[scratch.off];
  scratch.off += size;

  return ret;
}

static void
scratch_free (void *ptr)
{
  if ((char *)ptr >= scratch.buf && (char *)ptr < scratch.buf + scratch.off)
    return;
}

static void
hook_memtable (void)
{
  if (hooked)
    return;

  hooked = 1;

  real_calloc = dlsym (RTLD_NEXT, "calloc");
  real_free = dlsym (RTLD_NEXT, "free");
  real_malloc = dlsym (RTLD_NEXT, "malloc");
  real_realloc = dlsym (RTLD_NEXT, "realloc");
  real_aligned_alloc = dlsym (RTLD_NEXT, "aligned_alloc");
  real_posix_memalign = dlsym (RTLD_NEXT, "posix_memalign");
  real_memalign = dlsym (RTLD_NEXT, "memalign");

  unsetenv ("LD_PRELOAD");
}

static inline void
track_malloc (void   *ptr,
              size_t  size)
{
  if G_LIKELY (collector_ready != FALSE)
    sysprof_collector_allocate (GPOINTER_TO_SIZE (ptr),
                                size,
                                backtrace_func,
                                NULL);
}

static inline void
track_free (void *ptr)
{
  if G_LIKELY (collector_ready != FALSE)
    sysprof_collector_allocate (GPOINTER_TO_SIZE (ptr),
                                0,
                                backtrace_func,
                                NULL);
}

void *
malloc (size_t size)
{
  void *ret = real_malloc (size);
  track_malloc (ret, size);
  return ret;
}

void *
calloc (size_t nmemb,
        size_t size)
{
  void *ret = real_calloc (nmemb, size);
  track_malloc (ret, size);
  return ret;
}

void *
realloc (void   *ptr,
         size_t  size)
{
  void *ret = real_realloc (ptr, size);

  if (ret != ptr)
    {
      track_free (ptr);
      track_malloc (ret, size);
    }

  return ret;
}

void
free (void *ptr)
{
  real_free (ptr);
  track_free (ptr);
}

void *
aligned_alloc (size_t alignment,
               size_t size)
{
  void *ret = real_aligned_alloc (alignment, size);
  track_malloc (ret, size);
  return ret;
}

int
posix_memalign (void   **memptr,
                size_t   alignment,
                size_t   size)
{
  int ret = real_posix_memalign (memptr, alignment, size);
  track_malloc (*memptr, size);
  return ret;
}

void *
memalign (size_t alignment,
          size_t size)
{
  void *ret = real_memalign (alignment, size);
  track_malloc (ret, size);
  return ret;
}
