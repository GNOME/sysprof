/* -*- c-basic-offset: 8 -*- */

/* Sysprof -- Sampling, systemwide CPU profiler
 * Copyright 2004, Red Hat, Inc.
 * Copyright 2004, 2005, 2006, 2007, 2008, Soeren Sandmann
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef CONFIG_SMP
# define __SMP__
#endif
#include <asm/atomic.h>
#include <linux/kernel.h>  /* Needed for KERN_ALERT */
#include <linux/module.h>  /* Needed by all modules */
#include <linux/sched.h>

#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/poll.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/profile.h>

#include "sysprof-module.h"

#include "../config.h"

#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)
#include <linux/config.h>
#endif

#if !CONFIG_PROFILING
# error Sysprof needs a kernel with profiling support compiled in.
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,11)
# error Sysprof needs a Linux 2.6.11 kernel or later
#endif
#include <linux/kallsyms.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Soeren Sandmann (sandmann@daimi.au.dk)");

#define SAMPLES_PER_SECOND (200)
#define INTERVAL ((HZ <= SAMPLES_PER_SECOND)? 1 : (HZ / SAMPLES_PER_SECOND))
#define N_TRACES 256

static SysprofStackTrace	stack_traces[N_TRACES];
static SysprofStackTrace *	head = &stack_traces[0];
static SysprofStackTrace *	tail = &stack_traces[0];
DECLARE_WAIT_QUEUE_HEAD (wait_for_trace);
DECLARE_WAIT_QUEUE_HEAD (wait_for_exit);

/* Macro the names of the registers that are used on each architecture */
#if !defined(CONFIG_X86_64) && !defined(CONFIG_X86)
#       error Sysprof only supports the i386 and x86-64 architectures
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION (2,6,25)
#       define REG_FRAME_PTR bp
#       define REG_INS_PTR ip
#       define REG_STACK_PTR sp
#       define REG_STACK_PTR0 sp0
#else
#       if defined(CONFIG_X86_64)
#               define REG_FRAME_PTR rbp
#               define REG_INS_PTR rip
#               define REG_STACK_PTR rsp
#               define REG_STACK_PTR0 rsp0
#       else
#               define REG_FRAME_PTR ebp
#               define REG_INS_PTR eip
#               define REG_STACK_PTR esp
#               define REG_STACK_PTR0 esp0
#       endif
#endif

typedef struct userspace_reader userspace_reader;
struct userspace_reader
{
	struct task_struct *task;
	unsigned long cache_address;
	unsigned long *cache;
};

typedef struct StackFrame StackFrame;
struct StackFrame {
	unsigned long next;
	unsigned long return_address;
};

struct work_struct work;

static int
read_frame (void *frame_pointer, StackFrame *frame)
{
#if 0
	/* This is commented out because we seem to be called with
	 * (current_thread_info()->addr_limit.seg)) == 0
	 * which means access_ok() _always_ fails.
	 *
	 * Not sure why (or if) this isn't the case for oprofile
	 */
	if (!access_ok(VERIFY_READ, frame_pointer, sizeof(StackFrame)))
		return 1;
#endif

	if (__copy_from_user_inatomic (
		    frame, frame_pointer, sizeof (StackFrame)))
		return 1;
	
	return 0;
}

DEFINE_PER_CPU(int, n_samples);

static int
timer_notify (struct pt_regs *regs)
{
	SysprofStackTrace *trace = head;
	int i;
	int is_user;
	static atomic_t in_timer_notify = ATOMIC_INIT(1);
	int n;

	n = ++get_cpu_var(n_samples);
	put_cpu_var(n_samples);

	if (n % INTERVAL != 0)
		return 0;

	/* 0: locked, 1: unlocked */
	
	if (!atomic_dec_and_test(&in_timer_notify))
		goto out;
	
	is_user = user_mode(regs);

	if (!current || current->pid == 0)
		goto out;
	
	if (is_user && current->state != TASK_RUNNING)
		goto out;

	if (!is_user)
	{
		/* kernel */
		
		trace->pid = current->pid;
		trace->truncated = 0;
		trace->n_addresses = 1;

		/* 0x1 is taken by sysprof to mean "in kernel" */
		trace->addresses[0] = (void *)0x1;
	}
	else
	{
		StackFrame *frame_pointer;
		StackFrame frame;
		memset(trace, 0, sizeof (SysprofStackTrace));
		
		trace->pid = current->pid;
		trace->truncated = 0;

		i = 0;
		
		trace->addresses[i++] = (void *)regs->REG_INS_PTR;
		
		frame_pointer = (void *)regs->REG_FRAME_PTR;
	
		while (read_frame (frame_pointer, &frame) == 0		&&
		       i < SYSPROF_MAX_ADDRESSES			&&
		       (unsigned long)frame_pointer >= regs->REG_STACK_PTR)
		{
			trace->addresses[i++] = (void *)frame.return_address;
			frame_pointer = (StackFrame *)frame.next;
		}
		
		trace->n_addresses = i;

		if (i == SYSPROF_MAX_ADDRESSES)
			trace->truncated = 1;
		else
			trace->truncated = 0;
	}
	
	if (head++ == &stack_traces[N_TRACES - 1])
		head = &stack_traces[0];
	
	wake_up (&wait_for_trace);

out:
	atomic_inc(&in_timer_notify);
	return 0;
}

static int
procfile_read(char *buffer, 
	      char **buffer_location, 
	      off_t offset, 
	      int buffer_len,
	      int *eof,
	      void *data)
{
	if (head == tail)
		return -EWOULDBLOCK;
	
	*buffer_location = (char *)tail;

	BUG_ON(tail->pid == 0);
	
	if (tail++ == &stack_traces[N_TRACES - 1])
		tail = &stack_traces[0];
	
	return sizeof (SysprofStackTrace);
}

struct proc_dir_entry *trace_proc_file;
static unsigned int
procfile_poll(struct file *filp, poll_table *poll_table)
{
	if (head != tail)
		return POLLIN | POLLRDNORM;
	
	poll_wait(filp, &wait_for_trace, poll_table);

	if (head != tail)
		return POLLIN | POLLRDNORM;
	
	return 0;
}

int
init_module(void)
{
	static struct file_operations fops;

	trace_proc_file =
		create_proc_entry ("sysprof-trace", S_IFREG | S_IRUGO, NULL);
	
	if (!trace_proc_file)
		return 1;

	fops = *trace_proc_file->proc_fops;
	fops.poll = procfile_poll;
	
	trace_proc_file->read_proc = procfile_read;
	trace_proc_file->proc_fops = &fops;
	trace_proc_file->size = sizeof (SysprofStackTrace);

	register_timer_hook (timer_notify);
	
	printk(KERN_ALERT "sysprof: loaded (%s)\n", PACKAGE_VERSION);
	
	return 0;
}

void
cleanup_module(void)
{
	unregister_timer_hook (timer_notify);
	
	remove_proc_entry("sysprof-trace", NULL);

	printk(KERN_ALERT "sysprof: unloaded\n");
}

