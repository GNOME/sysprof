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

#ifdef CONFIG_SMP
# define __SMP__
#endif
#include <asm/atomic.h>
#include <linux/kernel.h>  /* Needed for KERN_ALERT */
#include <linux/module.h>  /* Needed by all modules */
#include <linux/sched.h>
#include <linux/miscdevice.h>

#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/poll.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/profile.h>

#include "../config.h"
#include "sysprof-module.h"

#include <linux/version.h>
#if (KERNEL_VERSION(2,6,9) > LINUX_VERSION_CODE) || (!CONFIG_PROFILING)
# error Sysprof needs a Linux 2.6.9 kernel or later, with profiling support compiled in.
#endif

#if (KERNEL_VERSION(2,6,11) > LINUX_VERSION_CODE)
#define OLD_PROFILE
#include <linux/notifier.h>
#endif

#include <linux/kallsyms.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Soeren Sandmann (sandmann@daimi.au.dk)");

static unsigned char	area_backing [sizeof (SysprofMmapArea) + PAGE_SIZE];
static SysprofMmapArea *area;
static atomic_t		client_count = ATOMIC_INIT(0);

DECLARE_WAIT_QUEUE_HEAD (wait_for_trace);

/* Macro the names of the registers that are used on each architecture */
#if defined(CONFIG_X86_64)
# define REG_FRAME_PTR rbp
# define REG_INS_PTR rip
# define REG_STACK_PTR rsp
# define REG_STACK_PTR0 rsp0
#elif defined(CONFIG_X86)
# define REG_FRAME_PTR ebp
# define REG_INS_PTR eip
# define REG_STACK_PTR esp
# define REG_STACK_PTR0 esp0
#else
# error Sysprof only supports the i386 and x86-64 architectures
#endif

#define SAMPLES_PER_SECOND 250
#define INTERVAL ((HZ <= SAMPLES_PER_SECOND)? 1 : (HZ / SAMPLES_PER_SECOND))

typedef struct StackFrame StackFrame;
struct StackFrame {
	unsigned long next;
	unsigned long return_address;
};

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
minimum (int a, int b)
{
	return a > b ? b : a;
}

#ifdef OLD_PROFILE
static int timer_notify(struct notifier_block * self, unsigned long val, void * data)
#else
static int
timer_notify (struct pt_regs *regs)
#endif
{
#ifdef OLD_PROFILE
	struct pt_regs * regs = (struct pt_regs *)data;
#endif
	void *frame_pointer;
	SysprofStackTrace *trace = &(area->traces[area->head]);
	int i;
	int is_user;
	StackFrame frame;
	int result;
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

	if (!current || current->pid == 0 || !current->mm)
		goto out;
	
	if (is_user && current->state != TASK_RUNNING)
		goto out;

	memset(trace, 0, sizeof (SysprofStackTrace));
	
	trace->pid = current->pid;

	trace->n_kernel_words = 0;
	trace->n_addresses = 0;

	i = 0;
	if (!is_user)
	{
		int n_bytes;
		char *esp;
		char *eos;

		trace->kernel_stack[0] = (void *)regs->REG_INS_PTR;
		trace->n_kernel_words = 1;
		
		/* The timer interrupt happened in kernel mode. When this
		 * happens the registers are pushed on the stack, _except_
		 * esp. So we can't use regs->esp to copy the stack pointer.
		 * Instead we use the fact that the regs pointer itself
		 * points to the stack.
		 */
		esp = (char *)regs + sizeof (struct pt_regs);
		eos = (char *)current->thread.REG_STACK_PTR0 - sizeof (struct pt_regs);
		
		n_bytes = minimum ((char *)eos - esp,
				   sizeof (trace->kernel_stack));

		if (n_bytes > 0) {
			memcpy (&(trace->kernel_stack[1]), esp, n_bytes);
			
			trace->n_kernel_words += (n_bytes) / sizeof (void *);
		}

		/* Now trace the user stack */
		regs = (struct pt_regs *)eos;
	}

	i = 0;
	trace->addresses[i++] = (void *)regs->REG_INS_PTR;

	frame_pointer = (void *)regs->REG_FRAME_PTR;
	
	while (((result = read_frame (frame_pointer, &frame)) == 0)	&&
	       i < SYSPROF_MAX_ADDRESSES				&&
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
	
	if (area->head++ == SYSPROF_N_TRACES - 1)
		area->head = 0;
	
	wake_up (&wait_for_trace);

out:
	atomic_inc(&in_timer_notify);
	
	return 0;
}

#ifdef OLD_PROFILE
static struct notifier_block timer_notifier = {
	.notifier_call  = timer_notify,
	NULL,
	0
};
#endif

static int
sysprof_read(struct file *file, char *buffer, size_t count, loff_t *offset)
{
	file->private_data = &(area->traces[area->head]);

	return 0;
}

static int
n_traces_available(SysprofStackTrace *tail)
{
	SysprofStackTrace *head = &(area->traces[area->head]);

	if (head >= tail)
		return head - tail;
	else
		return SYSPROF_N_TRACES - (tail - head);
}

static unsigned int
sysprof_poll(struct file *file, poll_table *poll_table)
{
	SysprofStackTrace *tail = file->private_data;

	if (n_traces_available (tail) >= 16)
		return POLLIN | POLLRDNORM;
	
	poll_wait(file, &wait_for_trace, poll_table);

	if (n_traces_available (tail) >= 16)
		return POLLIN | POLLRDNORM;
	
	return 0;
}

static int
sysprof_open(struct inode *inode, struct file *file)
{
	int retval = 0;

	if (atomic_inc_return(&client_count) == 1) {
#ifndef OLD_PROFILE
		retval = register_timer_hook (timer_notify);
#else
		retval = register_profile_notifier (&timer_notifier);
#endif
	}

	file->private_data = &(area->traces[area->head]);
	
	return retval;
}

static struct page *
sysprof_nopage(struct vm_area_struct *vma, unsigned long addr, int *type)
{
	unsigned long area_start;
	unsigned long virt;
	struct page *page_ptr;

#if 0
	printk (KERN_ALERT "nopage called: %p (offset: %d) area: %p\n", addr, addr - vma->vm_start, area);
#endif
	
	area_start = (unsigned long)area;
	
	virt = area_start + (addr - vma->vm_start);

	if (virt > area_start + sizeof (SysprofMmapArea))
		return NOPAGE_SIGBUS;

	page_ptr = vmalloc_to_page ((void *)virt);
	
	get_page (page_ptr);
	
	if (type)
		*type = VM_FAULT_MINOR;

	return page_ptr;
}

static int
sysprof_mmap(struct file *filp, struct vm_area_struct *vma)
{
	static struct vm_operations_struct ops = {
		.nopage = sysprof_nopage,
	};
	
	if (vma->vm_flags & (VM_WRITE | VM_EXEC))
		return -EPERM;

	vma->vm_flags |= VM_RESERVED;

	vma->vm_ops = &ops;
	
	return 0;
}

static int
sysprof_release(struct inode *inode, struct file *file)
{
	if (atomic_dec_return(&client_count) == 0) {
#ifndef OLD_PROFILE
		unregister_timer_hook (timer_notify);
#else
		unregister_profile_notifier (&timer_notifier);
#endif
	}
	
	return 0;
}

static struct file_operations sysprof_fops = {
	.owner =	THIS_MODULE,
        .read =         sysprof_read,
        .poll =         sysprof_poll,
        .open =         sysprof_open,
        .release =      sysprof_release,
	.mmap =		sysprof_mmap,
};

static struct miscdevice sysprof_miscdev = {
	MISC_DYNAMIC_MINOR,
	"sysprof-trace",
	&sysprof_fops
};

int
init_module(void)
{
	int ret;
	unsigned long area_page;

	ret = misc_register(&sysprof_miscdev);
	if (ret) {
		printk(KERN_ERR "sysprof: failed to register misc device\n");
		return ret;
	}

	area_page = ((unsigned long)area_backing + PAGE_SIZE - 1) & PAGE_MASK;
	area = (SysprofMmapArea *)area_page;
	area->head = 0;

	printk(KERN_ALERT "sysprof: loaded (%s)\n", PACKAGE_VERSION);

	return 0;
}

void
cleanup_module(void)
{
	misc_deregister(&sysprof_miscdev);

	printk(KERN_ALERT "sysprof: unloaded\n");
}


#if 0
/* The old userspace_reader code - someday it may be useful again */

typedef struct userspace_reader userspace_reader;
struct userspace_reader
{
	struct task_struct *task;
	unsigned long cache_address;
	unsigned long *cache;
};

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

static void
done_userspace_reader (userspace_reader *reader)
{
	kfree (reader->cache);
}

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

