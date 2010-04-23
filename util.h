#ifndef UTIL_H
#define UTIL_H

#define FMT64   "%"G_GUINT64_FORMAT

#if defined(__i386__)
#define rmb()           asm volatile("lock; addl $0,0(%%esp)" ::: "memory")
#define cpu_relax()     asm volatile("rep; nop" ::: "memory");
#endif

#if defined(__x86_64__)
#define rmb()           asm volatile("lfence" ::: "memory")
#define cpu_relax()     asm volatile("rep; nop" ::: "memory");
#endif

#ifdef __powerpc__
#define rmb()           asm volatile ("sync" ::: "memory")
#define cpu_relax()     asm volatile ("" ::: "memory");
#endif

#ifdef __s390__
#define rmb()           asm volatile("bcr 15,0" ::: "memory")
#define cpu_relax()     asm volatile("" ::: "memory");
#endif

#ifdef __sh__
#if defined(__SH4A__) || defined(__SH5__)
# define rmb()          asm volatile("synco" ::: "memory")
#else
# define rmb()          asm volatile("" ::: "memory")
#endif
#define cpu_relax()     asm volatile("" ::: "memory")
#endif

#ifdef __hppa__
#define rmb()           asm volatile("" ::: "memory")
#define cpu_relax()     asm volatile("" ::: "memory");
#endif

#endif
