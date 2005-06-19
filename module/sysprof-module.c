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

#define SAMPLES_PER_SECOND (256)
#define INTERVAL (HZ / SAMPLES_PER_SECOND)
#define N_TRACES 256

static SysprofStackTrace	stack_traces[N_TRACES];
static SysprofStackTrace *	head = &stack_traces[0];
static SysprofStackTrace *	tail = &stack_traces[0];
DECLARE_WAIT_QUEUE_HEAD (wait_for_trace);
DECLARE_WAIT_QUEUE_HEAD (wait_for_exit);

#if 0
static void on_timer(unsigned long);
#endif
#if 0
static struct timer_list timer;
#endif

typedef struct userspace_reader userspace_reader;
struct userspace_reader
{
	struct task_struct *task;
	unsigned long cache_address;
	unsigned long *cache;
};

#if 0
/* This function was mostly cutted and pasted from ptrace.c
 */

/* Access another process' address space.
 * Source/target buffer must be kernel space, 
 * Do not walk the page table directly, use get_user_pages
*/
static struct mm_struct *
get_mm (struct task_struct *tsk)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,9)
	struct mm_struct *mm;
	
	task_lock (tsk);
	mm = tsk->mm;
	task_unlock (tsk);

	return mm;
#else
	return get_task_mm (tsk);
#endif
}

static void
put_mm (struct mm_struct *mm)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,9)
	mmput(mm);
#endif
}
#endif

#if 0
static int
x_access_process_vm (struct task_struct *tsk,
		     unsigned long addr,
		     void *buf, int len, int write)
{
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	struct page *page;
	void *old_buf = buf;

	mm = get_mm (tsk);
	
	if (!mm)
		return 0;

	down_read(&mm->mmap_sem);
	/* ignore errors, just check how much was sucessfully transfered */
	while (len)
	{
		int bytes, ret, offset;
		void *maddr;

		ret = get_user_pages(tsk, mm, addr, 1,
				write, 1, &page, &vma);
		if (ret <= 0) {
			break;
		}
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

	put_mm (mm);
	
	return buf - old_buf;
}
#endif

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

#if 0
static struct pt_regs *
get_regs (struct task_struct *task)
{
	/* This seems to work in Linux 2.6.11 */
        void *stack_top = (void *)task->thread.esp0;
        return stack_top - sizeof(struct pt_regs);

#if 0
	/* This used to work, but doesn't anymore */
	return ((struct pt_regs *) (THREAD_SIZE + (unsigned long) task->thread_info)) - 1;
#endif

#if 0
	/* One might suspect that this would work, but it doesn't
	 */
	return task_pt_regs (task);
#endif
}
#endif

#if 0
static void
generate_stack_trace(struct task_struct *task,
		     SysprofStackTrace *trace)
{
	struct pt_regs *regs = get_regs (task);  // task->thread.regs;
	
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

#if 0
	printk (KERN_ALERT "ebp: %p\n", regs->ebp);
#endif
	
	if (init_userspace_reader (&reader, task))
	{
		while (i < SYSPROF_MAX_ADDRESSES &&
		       read_frame (&reader, addr, &frame) &&
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
#endif

#if 0
static void
do_generate (void *data)
{
	struct task_struct *task = data;

	generate_stack_trace(task, head);
			
	if (head++ == &stack_traces[N_TRACES - 1])
		head = &stack_traces[0];

	wake_up (&wait_for_trace);

	/* If a task dies between the time we see it in on_timer and
	 * the time we get here, it will be leaked. If __put_task_struct9)
	 * was exported, then we could do this properly
	 */

	atomic_dec (&(task)->usage);
	
	mod_timer(&timer, jiffies + INTERVAL);
}
#endif

struct work_struct work;

#if 0
static void
on_timer(unsigned long dong)
{
	if (current && current->state == TASK_RUNNING && current->pid != 0)
	{
		get_task_struct (current);

		INIT_WORK (&work, do_generate, current);

		schedule_work (&work);
	}
	else
	{
		mod_timer(&timer, jiffies + INTERVAL);
	}
}
#endif

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

static int
timer_notify (struct pt_regs *regs)
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

	if ((n_samples++ % INTERVAL) != 0)
		return 0;

	is_user = user_mode(regs);

	if (!current || current->pid == 0)
		return 0;
	
	if (is_user && current->state != TASK_RUNNING)
		return 0;

	if (!is_user)
	{
		/* kernel */
		
		trace->pid = -1;
		trace->truncated = 0;
		trace->n_addresses = 1;
		trace->addresses[0] = 0x0;
	}
	else
	{
		memset(trace, 0, sizeof (SysprofStackTrace));
		
		trace->pid = current->pid;
		trace->truncated = 0;
		
		trace->addresses[0] = (void *)regs->eip;
		
		i = 1;
		
		frame = (StackFrame *)regs->ebp;
		
		while (pages_present (frame)				&&
		       i < SYSPROF_MAX_ADDRESSES			&&
		       ((unsigned long)frame) < START_OF_STACK		&&
		       (unsigned long)frame >= regs->esp)
		{
			trace->addresses[i++] = (void *)frame->return_address;
			frame = (StackFrame *)frame->next;
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
	
#if 0
	init_timer(&timer);
	timer.function = on_timer;
	mod_timer(&timer, jiffies + INTERVAL);
#endif
	
	printk(KERN_ALERT "starting sysprof module\n");
	
	return 0;
}

void
cleanup_module(void)
{
#if 0
	del_timer (&timer);
#endif

	unregister_timer_hook (timer_notify);
	
	remove_proc_entry("sysprof-trace", &proc_root);
}

