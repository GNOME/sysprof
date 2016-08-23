#ifndef SP_UTIL_H
#define SP_UTIL_H

#ifdef __i386__
#define read_barrier()           asm volatile("lock; addl $0,0(%%esp)" ::: "memory")
#endif

#ifdef __x86_64__
#define read_barrier()           asm volatile("lfence" ::: "memory")
#endif

#ifdef __powerpc__
#define read_barrier()           asm volatile ("sync" ::: "memory")
#endif

#ifdef __s390__
#define read_barrier()           asm volatile("bcr 15,0" ::: "memory")
#endif

#ifdef __sh__
#if defined(__SH4A__) || defined(__SH5__)
# define read_barrier()          asm volatile("synco" ::: "memory")
#else
# define read_barrier()          asm volatile("" ::: "memory")
#endif
#endif

#ifdef __hppa__
#define read_barrier()           asm volatile("" ::: "memory")
#endif

#ifdef __arm__
#define read_barrier()           asm volatile("dsb" ::: "memory")
#endif

/*
 * Fallback to a full memory barrier if the architecture is not yet
 * supported with a lighter read barrier.
 */
#ifndef read_barrier
#define read_barrier()           __sync_synchronize()
#endif

#endif /* SP_UTIL_H */
