/*
 * sysprof-live-pid.c
 *
 * Copyright 2024 Christian Hergert <chergert@redhat.com>
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

#include <fcntl.h>
#include <unistd.h>

#include <sys/wait.h>
#include <sys/syscall.h>

/* Workaround for linux/fcntl.h also including the
 * flock definitions in addition to libc.
 */
#ifndef _LINUX_FCNTL_H
# define _LINUX_FCNTL_H
# include <linux/pidfd.h>
# undef _LINUX_FCNTL_H
#else
# include <linux/pidfd.h>
#endif

#include <linux/perf_event.h>

#include <libelf.h>
#include <elfutils/libdwelf.h>
#include <elfutils/libdwfl.h>

#include <glib/gstdio.h>

#include <sysprof-capture.h>

#include "sysprof-live-process.h"

typedef struct _SysprofLiveProcess
{
  Dwfl_Callbacks  callbacks;
  Dwfl           *dwfl;
  Elf            *elf;
  char           *root;
  GPid            pid;
  int             fd;
} SysprofLiveProcess;

typedef struct _SysprofUnwinder
{
  SysprofLiveProcess *process;
  const guint8       *stack;
  gsize               stack_len;
  const guint64      *registers;
  guint               n_registers;
  guint64            *addresses;
  guint               addresses_capacity;
  guint               addresses_len;
  guint64             abi;
  guint64             sp;
  guint64             pc;
  guint64             base_address;
  GPid                pid;
  GPid                tid;
} SysprofUnwinder;

static SysprofUnwinder *current_unwinder;

#if defined(__x86_64__) || defined(__i386__)
static inline GPid
sysprof_unwinder_next_thread (Dwfl  *dwfl,
                              void  *user_data,
                              void **thread_argp)
{
  SysprofUnwinder *unwinder = current_unwinder;

  if (*thread_argp == NULL)
    {
      *thread_argp = unwinder;
      return unwinder->tid;
    }

  return 0;
}

static inline bool
sysprof_unwinder_get_thread (Dwfl   *dwfl,
                             pid_t   tid,
                             void   *user_data,
                             void  **thread_argp)
{
  SysprofUnwinder *unwinder = current_unwinder;

  if (unwinder->tid == tid)
    {
      *thread_argp = unwinder;
      return TRUE;
    }

  return FALSE;
}

static inline bool
copy_word (const guint8 *data,
           Dwarf_Word   *result,
           guint64       abi)
{
  if (abi == PERF_SAMPLE_REGS_ABI_64)
    memcpy (result, data, sizeof (guint64));
  else if (abi == PERF_SAMPLE_REGS_ABI_32)
    memcpy (result, data, sizeof (guint32));
  else
    g_assert_not_reached ();

  return TRUE;
}

static inline bool
sysprof_unwinder_memory_read (Dwfl       *dwfl,
                              Dwarf_Addr  addr,
                              Dwarf_Word *result,
                              void       *user_data)
{
  SysprofUnwinder *unwinder = current_unwinder;

  if (addr < unwinder->base_address || addr - unwinder->base_address >= unwinder->stack_len)
    {
      Dwfl_Module *module = NULL;
      Elf_Data *data = NULL;
      Elf_Scn *section = NULL;
      Dwarf_Addr bias;

      if (!(module = dwfl_addrmodule (dwfl, addr)) ||
          !(section = dwfl_module_address_section (module, &addr, &bias)) ||
          !(data = elf_getdata (section, NULL)))
        return FALSE;

      if (data->d_buf && data->d_size > addr)
        return copy_word ((guint8 *)data->d_buf + addr, result, unwinder->abi);

      return FALSE;
    }

  return copy_word (&unwinder->stack[addr - unwinder->base_address], result, unwinder->abi);
}

static inline bool
sysprof_unwinder_set_initial_registers (Dwfl_Thread *thread,
                                        void        *user_data)
{
  SysprofUnwinder *unwinder = current_unwinder;

  dwfl_thread_state_register_pc (thread, unwinder->pc);

  if (unwinder->abi == PERF_SAMPLE_REGS_ABI_64)
    {
      static const int regs_x86_64[] = {0, 3, 2, 1, 4, 5, 6, 7/*sp*/, 9, 10, 11, 12, 13, 14, 15, 16, 8/*ip*/};

      for (int i = 0; i < G_N_ELEMENTS (regs_x86_64); i++)
        {
          int j = regs_x86_64[i];

          dwfl_thread_state_registers (thread, i, 1, &unwinder->registers[j]);
        }
    }
  else if (unwinder->abi == PERF_SAMPLE_REGS_ABI_32)
    {
      static const int regs_i386[] = {0, 2, 3, 1, 7/*sp*/, 6, 4, 5, 8/*ip*/};

      for (int i = 0; i < G_N_ELEMENTS (regs_i386); i++)
        {
          int j = regs_i386[i];

          dwfl_thread_state_registers (thread, i, 1, &unwinder->registers[j]);
        }
    }

  return TRUE;
}

static const Dwfl_Thread_Callbacks thread_callbacks = {
  sysprof_unwinder_next_thread,
  sysprof_unwinder_get_thread,
  sysprof_unwinder_memory_read,
  sysprof_unwinder_set_initial_registers,
  NULL, /* detach */
  NULL, /* thread_detach */
};

static inline int
sysprof_unwinder_frame_cb (Dwfl_Frame *frame,
                           void       *user_data)
{
  SysprofUnwinder *unwinder = current_unwinder;
  Dwarf_Addr pc;
  Dwarf_Addr sp;
  bool is_activation;
  guint8 sp_register_id;

  if (unwinder->addresses_len == unwinder->addresses_capacity)
    return DWARF_CB_ABORT;

  if (!dwfl_frame_pc (frame, &pc, &is_activation))
    return DWARF_CB_ABORT;

  if (unwinder->abi == PERF_SAMPLE_REGS_ABI_64)
    sp_register_id = 7;
  else if (unwinder->abi == PERF_SAMPLE_REGS_ABI_32)
    sp_register_id = 4;
  else
    return DWARF_CB_ABORT;

  if (dwfl_frame_reg (frame, sp_register_id, &sp) < 0)
    return DWARF_CB_ABORT;

  unwinder->addresses[unwinder->addresses_len++] = pc;

  return DWARF_CB_OK;
}
#endif

static inline guint
sysprof_unwind (SysprofLiveProcess *self,
                Dwfl               *dwfl,
                Elf                *elf,
                GPid                pid,
                GPid                tid,
                guint64             abi,
                const guint64      *registers,
                guint               n_registers,
                const guint8       *stack,
                gsize               stack_len,
                guint64            *addresses,
                guint               n_addresses)
{
#if defined(__x86_64__) || defined(__i386__)
  SysprofUnwinder unwinder;

  g_assert (dwfl != NULL);
  g_assert (elf != NULL);

  /* Ignore anything byt 32/64 defined abi */
  if (!(abi == PERF_SAMPLE_REGS_ABI_32 || abi == PERF_SAMPLE_REGS_ABI_64))
    return 0;

  /* Make sure we have registers/stack to work with */
  if (registers == NULL || stack == NULL || stack_len == 0)
    return 0;

  /* 9 registers on 32-bit x86, 17 on 64-bit x86_64 */
  if (!((abi == PERF_SAMPLE_REGS_ABI_32 && n_registers == 9) ||
        (abi == PERF_SAMPLE_REGS_ABI_64 && n_registers == 17)))
    return 0;

  unwinder.process = self;
  unwinder.sp = registers[7];
  unwinder.pc = registers[8];
  unwinder.base_address = unwinder.sp;
  unwinder.addresses = addresses;
  unwinder.addresses_capacity = n_addresses;
  unwinder.addresses_len = 0;
  unwinder.pid = pid;
  unwinder.tid = tid;
  unwinder.stack = stack;
  unwinder.stack_len = stack_len;
  unwinder.abi = abi;
  unwinder.registers = registers;
  unwinder.n_registers = n_registers;

  current_unwinder = &unwinder;
  dwfl_getthread_frames (dwfl, tid, sysprof_unwinder_frame_cb, NULL);
  current_unwinder = NULL;

  return unwinder.addresses_len;
#else
  return 0;
#endif
}

G_GNUC_NO_INLINE static int
_pidfd_open (int      pid,
             unsigned flags)
{
  int pidfd = syscall (SYS_pidfd_open, pid, flags);

  if (pidfd != -1)
    {
      int old_flags = fcntl (pidfd, F_GETFD);

      if (old_flags != -1)
        fcntl (pidfd, F_SETFD, old_flags | FD_CLOEXEC);
    }

  return pidfd;
}

static int
sysprof_live_process_find_elf (Dwfl_Module  *module,
                               void        **user_data,
                               const char   *module_name,
                               Dwarf_Addr    base_addr,
                               char        **filename,
                               Elf         **elf)
{
  g_assert (current_unwinder != NULL);
  g_assert (current_unwinder->process != NULL);

  *filename = NULL;
  *elf = NULL;

  if (module_name[0] == '/')
    {
      g_autofree char *path = g_strdup_printf ("/proc/%u/root/%s", current_unwinder->pid, module_name);
      g_autofd int fd = open (path, O_RDONLY | O_CLOEXEC);

      if (fd != -1)
        {
          *elf = dwelf_elf_begin (fd);
          return g_steal_fd (&fd);
        }
    }

  return dwfl_linux_proc_find_elf (module, user_data, module_name, base_addr, filename, elf);
}

static int
sysprof_live_process_find_debuginfo (Dwfl_Module  *module,
                                     void        **user_data,
                                     const char   *module_name,
                                     Dwarf_Addr    base_addr,
                                     const char   *file_name,
                                     const char   *debuglink_file,
                                     GElf_Word     debuglink_crc,
                                     char        **debuginfo_file_name)
{
  return -1;
}

SysprofLiveProcess *
sysprof_live_process_new (GPid pid)
{
  SysprofLiveProcess *live_process;

  live_process = g_atomic_rc_box_new0 (SysprofLiveProcess);
  live_process->pid = pid;
  live_process->fd = _pidfd_open (pid, 0);
  live_process->root = g_strdup_printf ("/proc/%u/root/", pid);
  live_process->callbacks.find_elf = sysprof_live_process_find_elf;
  live_process->callbacks.find_debuginfo = sysprof_live_process_find_debuginfo;
  live_process->callbacks.debuginfo_path = g_new0 (char *, 2);
  live_process->callbacks.debuginfo_path[0] = g_build_filename (live_process->root, "usr/lib/debug", NULL);

  return live_process;
}

SysprofLiveProcess *
sysprof_live_process_ref (SysprofLiveProcess *live_process)
{
  g_return_val_if_fail (live_process != NULL, NULL);

  return g_atomic_rc_box_acquire (live_process);
}

static void
sysprof_live_process_finalize (gpointer data)
{
  SysprofLiveProcess *live_process = data;

  if (live_process->fd != -1)
    {
      close (live_process->fd);
      live_process->fd = -1;
    }

  g_clear_pointer (&live_process->elf, elf_end);
  g_clear_pointer (&live_process->dwfl, dwfl_end);
  g_clear_pointer (&live_process->root, g_free);
  g_clear_pointer (&live_process->callbacks.debuginfo_path, g_free);
}

void
sysprof_live_process_unref (SysprofLiveProcess *live_process)
{
  g_return_if_fail (live_process != NULL);

  g_atomic_rc_box_release_full (live_process, sysprof_live_process_finalize);
}

gboolean
sysprof_live_process_is_active (SysprofLiveProcess *self)
{
  g_return_val_if_fail (self != NULL, FALSE);

  return self->fd > -1;
}

#if defined(__x86_64__) || defined(__i386__)
static Dwfl *
sysprof_live_process_get_dwfl (SysprofLiveProcess *self)
{
  g_assert (self != NULL);

  if G_UNLIKELY (self->dwfl == NULL)
    {
      self->dwfl = dwfl_begin (&self->callbacks);

      dwfl_linux_proc_report (self->dwfl, self->pid);
      dwfl_report_end (self->dwfl, NULL, NULL);

      if (self->fd > -1)
        {
          char path[64];
          g_autofd int exe_fd = -1;

          g_snprintf (path, sizeof path, "/proc/%u/exe", self->pid);
          exe_fd = open (path, O_RDONLY);

          if (exe_fd > -1)
            {
              self->elf = elf_begin (exe_fd, ELF_C_READ_MMAP, NULL);

              if (self->elf != NULL)
                dwfl_attach_state (self->dwfl, self->elf, self->pid, &thread_callbacks, self);
            }
        }
      else
        g_warning ("Attmpting to load exited process\n");
    }

  return self->dwfl;
}
#endif

guint
sysprof_live_process_unwind (SysprofLiveProcess *self,
                             GPid                tid,
                             guint64             abi,
                             const guint8       *stack,
                             gsize               stack_len,
                             const guint64      *registers,
                             guint               n_registers,
                             guint64            *addresses,
                             guint               n_addresses)
{
#if defined(__x86_64__) || defined(__i386__)
  Dwfl *dwfl;

  g_assert (self != NULL);
  g_assert (stack != NULL);
  g_assert (registers != NULL);
  g_assert (addresses != NULL);

  if (!sysprof_live_process_is_active (self))
    return 0;

  if (!(dwfl = sysprof_live_process_get_dwfl (self)))
    return 0;

  if (self->elf == NULL)
    return 0;

  g_assert (self->dwfl != NULL);
  g_assert (self->elf != NULL);

  return sysprof_unwind (self,
                         self->dwfl,
                         self->elf,
                         self->pid,
                         tid,
                         abi,
                         registers,
                         n_registers,
                         stack,
                         stack_len,
                         addresses,
                         n_addresses);
#else
  return 0;
#endif
}

void
sysprof_live_process_add_map (SysprofLiveProcess *self,
                              guint64             begin,
                              guint64             end,
                              guint64             offset,
                              guint64             inode,
                              const char         *filename)
{
  g_assert (self != NULL);

  /* We'll reparse VMAs on next use */
  g_clear_pointer (&self->dwfl, dwfl_end);
  g_clear_pointer (&self->elf, elf_end);
}
