/* -*- c-basic-offset: 8 -*- */

/* Sysprof -- Sampling, systemwide CPU profiler
 * Copyright 2004, Red Hat, Inc.
 * Copyright 2004, 2005, Soeren Sandmann
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

#include <linux/config.h>
#if !CONFIG_PROFILING
# error Sysprof needs a kernel with profiling support compiled in.
#endif
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

#include <linux/version.h>
#if KERNEL_VERSION(2,6,11) > LINUX_VERSION_CODE
# error Sysprof needs a Linux 2.6.11 kernel or later
#endif
#include <linux/kallsyms.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Soeren Sandmann (sandmann@daimi.au.dk)");

#define N_TRACES 256

static SysprofStackTrace	stack_traces[N_TRACES];
static SysprofStackTrace *	head = &stack_traces[0];
static SysprofStackTrace *	tail = &stack_traces[0];
DECLARE_WAIT_QUEUE_HEAD (wait_for_trace);
DECLARE_WAIT_QUEUE_HEAD (wait_for_exit);

/* Macro the names of the registers that are used on each architecture */
#if defined(CONFIG_X86_64)
# define REG_FRAME_PTR rbp
# define REG_INS_PTR rip
# define REG_STACK_PTR rsp
#elif defined(CONFIG_X86)
# define REG_FRAME_PTR ebp
# define REG_INS_PTR eip
# define REG_STACK_PTR esp
#else
# error Sysprof only supports the i386 and x86-64 architectures
#endif

#define SAMPLES_PER_SECOND 250
#define INTERVAL ((HZ <= SAMPLES_PER_SECOND)? 1 : (HZ / SAMPLES_PER_SECOND))

typedef struct userspace_reader userspace_reader;
struct userspace_reader
{
	struct task_struct *task;
	unsigned long cache_address;
	unsigned long *cache;
};

#if 0
static int
init_userspace_reader (userspace_reader *reader,
		       struct task_struct *task)
{
	reader->task = task;
	reader->cache = kmalloc (PAGE_SIZE, GFP_KERNEL);
	if (!reader->cache)
		return 0;
	reader->cache_address = 0x0;
	return 1;
}
#endif

#if 0
static int
page_readable (userspace_reader *reader, unsigned long address)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION (2,6,11)
		struct mm_struct *mm;
		int result = 1;

		mm = get_mm (reader->task);

		if (!mm)
			return 0;
		
		if (!check_user_page_readable (reader->task->mm, address))
			result = 0;

		put_mm (mm);

		return result;
#endif
		
		return 1;
}
#endif

#if 0
static int
read_user_space (userspace_reader *reader,
		 unsigned long address,
		 unsigned long *result)
{
	unsigned long cache_address = reader->cache_address;
	int index, r;

	if (!cache_address || cache_address != (address & PAGE_MASK))
	{
		if (!page_readable (reader, address))
			return 0;
		
		cache_address = address & PAGE_MASK;

		r = x_access_process_vm (reader->task, cache_address,
					 reader->cache, PAGE_SIZE, 0);

		if (r != PAGE_SIZE) {
			return 0;
		}

		reader->cache_address = cache_address;
	}

	index = (address - cache_address) / sizeof (unsigned long);
	
	*result = reader->cache[index];
	return 1;
}
#endif

#if 0
static void
done_userspace_reader (userspace_reader *reader)
{
	kfree (reader->cache);
}
#endif

typedef struct StackFrame StackFrame;
struct StackFrame {
	unsigned long next;
	unsigned long return_address;
};

#if 0
static int
read_frame (userspace_reader *reader, unsigned long addr, StackFrame *frame)
{
	if (!addr || !frame)
		return 0;

	frame->next = 0;
	frame->return_address = 0;
	
	if (!read_user_space (reader, addr, &(frame->next)))
		return 0;

	if (!read_user_space (reader, addr + 4, &(frame->return_address)))
		return 0;

	return 1;
}
#endif

struct work_struct work;

/**
 * pages_present() from OProfile
 *
 * Copyright 2002 OProfile authors
 *
 * author John Levon
 * author David Smith
 */

#ifdef CONFIG_X86_4G
/* With a 4G kernel/user split, user pages are not directly
 * accessible from the kernel, so don't try
 */
static int pages_present(StackFrame * head)
{
	return 0;
}
#else
/* check that the page(s) containing the frame head are present */
static int pages_present(StackFrame * head)
{
	struct mm_struct * mm = current->mm;

	/* FIXME: only necessary once per page */
	if (!check_user_page_readable(mm, (unsigned long)head))
		return 0;

	return check_user_page_readable(mm, (unsigned long)(head + 1));
}
#endif /* CONFIG_X86_4G */

static int timer_notify (struct pt_regs *regs)
{
#ifdef CONFIG_HIGHMEM
#  define START_OF_STACK 0xFF000000
#else
#  define START_OF_STACK 0xBFFFFFFF
#endif
	StackFrame *frame;
	static int n_samples;
	SysprofStackTrace *trace = head;
	int i;
	int is_user;

	if ((++n_samples % INTERVAL) != 0)
		return 0;
	
	is_user = user_mode(regs);

	if (!current || current->pid == 0 || !current->mm)
		return 0;
	
	if (is_user && current->state != TASK_RUNNING)
		return 0;

	memset(trace, 0, sizeof (SysprofStackTrace));
	
	trace->pid = current->pid;
	
	i = 0;
	if (!is_user)
	{
		trace->addresses[i++] = 0x01;
		regs = (void *)current->thread.esp0 - sizeof (struct pt_regs);
	}

	trace->addresses[i++] = (void *)regs->REG_INS_PTR;
	
	frame = (StackFrame *)regs->REG_FRAME_PTR;
	
	while (pages_present (frame)				&&
	       i < SYSPROF_MAX_ADDRESSES			&&
	       ((unsigned long)frame) < START_OF_STACK		&&
	       (unsigned long)frame >= regs->REG_STACK_PTR)
	{
		trace->addresses[i++] = (void *)frame->return_address;
		frame = (StackFrame *)frame->next;
	}
	
	trace->n_addresses = i;
	
	if (i == SYSPROF_MAX_ADDRESSES)
		trace->truncated = 1;
	else
		trace->truncated = 0;
	
	if (head++ == &stack_traces[N_TRACES - 1])
		head = &stack_traces[0];
	
	wake_up (&wait_for_trace);
	
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
	trace_proc_file =
		create_proc_entry ("sysprof-trace", S_IFREG | S_IRUGO, &proc_root);
	
	if (!trace_proc_file)
		return 1;
	
	trace_proc_file->read_proc = procfile_read;
	trace_proc_file->proc_fops->poll = procfile_poll;
	trace_proc_file->size = sizeof (SysprofStackTrace);

	register_timer_hook (timer_notify);
	
	printk(KERN_ALERT "sysprof: loaded\n");
	
	return 0;
}

void
cleanup_module(void)
{
	unregister_timer_hook (timer_notify);
	
	remove_proc_entry("sysprof-trace", &proc_root);

	printk(KERN_ALERT "sysprof: unloaded\n");
}

