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

#include "sysprof-module.h"

#include <linux/version.h>
#include <linux/kallsyms.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Soeren Sandmann (sandmann@daimi.au.dk)");

#define SAMPLES_PER_SECOND (100)
#define INTERVAL (HZ / SAMPLES_PER_SECOND)
#define N_TRACES 256

static SysprofStackTrace	stack_traces[N_TRACES];
static SysprofStackTrace *	head = &stack_traces[0];
static SysprofStackTrace *	tail = &stack_traces[0];
DECLARE_WAIT_QUEUE_HEAD (wait_for_trace);
DECLARE_WAIT_QUEUE_HEAD (wait_for_exit);

static int exiting;

static void on_timer(unsigned long);
static struct timer_list timer;

typedef struct userspace_reader userspace_reader;
struct userspace_reader
{
	struct task_struct *task;
	unsigned long cache_address;
	unsigned long *cache;
};

/* This function was mostly cutted and pasted from ptrace.c
 */

/* Access another process' address space.
 * Source/target buffer must be kernel space, 
 * Do not walk the page table directly, use get_user_pages
 */
 
static int
x_access_process_vm(struct task_struct *tsk, unsigned long addr, void *buf, int len, int write)
{
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	struct page *page;
	void *old_buf = buf;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,9)
	task_lock (tsk);
	mm = tsk->mm;
	task_unlock (tsk);
#else
	mm = get_task_mm (tsk);
#endif
	
	if (!mm)
		return 0;

	down_read(&mm->mmap_sem);
	/* ignore errors, just check how much was sucessfully transfered */
	while (len) {
		int bytes, ret, offset;
		void *maddr;

		ret = get_user_pages(tsk, mm, addr, 1,
				write, 1, &page, &vma);
		if (ret <= 0)
			break;

		bytes = len;
		offset = addr & (PAGE_SIZE-1);
		if (bytes > PAGE_SIZE-offset)
			bytes = PAGE_SIZE-offset;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,11)

		flush_cache_page(vma, addr);

#endif
		
		maddr = kmap(page);
		if (write) {
			copy_to_user_page(vma, page, addr,
					  maddr + offset, buf, bytes);
			set_page_dirty_lock(page);
		} else {
			copy_from_user_page(vma, page, addr,
					    buf, maddr + offset, bytes);
		}
		kunmap(page);
		page_cache_release(page);
		len -= bytes;
		buf += bytes;
		addr += bytes;
	}
	up_read(&mm->mmap_sem);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,9)
	mmput(mm);
#endif
	
	return buf - old_buf;
}

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

static int
read_user_space (userspace_reader *reader,
		 unsigned long address,
		 unsigned long *result)
{
	unsigned long cache_address = reader->cache_address;
	int index, r;

	if (!cache_address || cache_address != (address & PAGE_MASK)) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION (2,6,11)
		if (!check_user_page_readable (reader->task->mm, address))
			return 0;
#endif
		
		cache_address = address & PAGE_MASK;

		r = x_access_process_vm (reader->task, cache_address,
					 reader->cache, PAGE_SIZE, 0);

		if (r != PAGE_SIZE)
			return 0;

		reader->cache_address = cache_address;
	}

	index = (address - cache_address) / sizeof (unsigned long);
	
	*result = reader->cache[index];
	return 1;
}

static void
done_userspace_reader (userspace_reader *reader)
{
	kfree (reader->cache);
}

typedef struct StackFrame StackFrame;
struct StackFrame {
	unsigned long next;
	unsigned long return_address;
};

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

static void
generate_stack_trace(struct task_struct *task,
		     SysprofStackTrace *trace)
{
#ifdef CONFIG_HIGHMEM
#  define START_OF_STACK 0xFF000000
#else
#  define START_OF_STACK 0xBFFFFFFF
#endif
	struct pt_regs *regs = ((struct pt_regs *) (THREAD_SIZE + (unsigned long) task->thread_info)) - 1;
	StackFrame frame;
	unsigned long addr;
	userspace_reader reader;
	int i;

	memset(trace, 0, sizeof (SysprofStackTrace));
	
	trace->pid = task->pid;
	trace->truncated = 0;

	trace->addresses[0] = (void *)regs->eip;

	i = 1;

	addr = regs->ebp;

	if (init_userspace_reader (&reader, task))
	{
		while (i < SYSPROF_MAX_ADDRESSES &&
		       read_frame (&reader, addr, &frame)  &&
		       addr < START_OF_STACK && addr >= regs->esp)
		{
			trace->addresses[i++] = (void *)frame.return_address;
			addr = frame.next;
		}
		
		done_userspace_reader (&reader);
	}

	trace->n_addresses = i;
	if (i == SYSPROF_MAX_ADDRESSES)
		trace->truncated = 1;
	else
		trace->truncated = 0;
}

DECLARE_WAIT_QUEUE_HEAD (wait_to_be_scanned);

static void
do_generate (void *data)
{
	struct task_struct *task = data;

	generate_stack_trace(task, head);
	wake_up_process (task);
		
	if (head++ == &stack_traces[N_TRACES - 1])
		head = &stack_traces[0];
	
	wake_up (&wait_for_trace);

	mod_timer(&timer, jiffies + INTERVAL);
}

struct work_struct work;

static void
on_timer(unsigned long dong)
{
	if (current && current->state == TASK_RUNNING && current->pid != 0)
	{
		set_current_state (TASK_UNINTERRUPTIBLE);
		
		INIT_WORK (&work, do_generate, current);
		
		schedule_work (&work);
	}
	else
	{
		mod_timer(&timer, jiffies + INTERVAL);
	}
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
	wait_event_interruptible (wait_for_trace, head != tail);
	*buffer_location = (char *)tail;
	if (tail++ == &stack_traces[N_TRACES - 1])
		tail = &stack_traces[0];
	return sizeof (SysprofStackTrace);
}

struct proc_dir_entry *trace_proc_file;
static unsigned int
procfile_poll(struct file *filp, poll_table *poll_table)
{
	if (head != tail) {
		return POLLIN | POLLRDNORM;
	}
	poll_wait(filp, &wait_for_trace, poll_table);
	if (head != tail) {
		return POLLIN | POLLRDNORM;
	}
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
	
	init_timer(&timer);
	timer.function = on_timer;
	mod_timer(&timer, jiffies + INTERVAL);
	
	printk(KERN_ALERT "starting sysprof module\n");
	
	return 0;
}

void
cleanup_module(void)
{
	exiting = 1;

	del_timer (&timer);

	remove_proc_entry("sysprof-trace", &proc_root);
	
	printk(KERN_ALERT "stopping sysprof module\n");
}
