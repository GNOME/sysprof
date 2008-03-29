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
#include <asm/stacktrace.h>
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
#	define REG_FRAME_PTR rbp
#	define REG_INS_PTR rip
#	define REG_STACK_PTR rsp
#	define REG_STACK_PTR0 rsp0
#elif defined(CONFIG_X86)
#	if LINUX_VERSION_CODE >= KERNEL_VERSION (2,6,25)
#		define REG_FRAME_PTR bp
#		define REG_INS_PTR ip
#		define REG_STACK_PTR sp
#		define REG_STACK_PTR0 sp0
#	else
#		define REG_FRAME_PTR ebp
#		define REG_INS_PTR eip
#		define REG_STACK_PTR esp
#		define REG_STACK_PTR0 esp0
#	endif
#else
#	error Sysprof only supports the i386 and x86-64 architectures
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

static void
nt_memcpy (void *dst, void *src, int n_bytes)
{
#if defined(CONFIG_X86_64) || defined(CONFIG_X86)
	if (((unsigned long)dst & 3)	||
	    ((unsigned long)src & 3)) {
		memcpy (dst, src, n_bytes);
	}
	else {
		int i;
		if ((unsigned long)src & 7) {
			*(uint32_t *)dst = *(uint32_t *)src;
			src += 4;
			dst += 4;
		}

		for (i = 0; i < n_bytes; i += 8) {
			__asm__ __volatile__ (
				"  movq  (%0, %2),  %%mm0\n"
				"  movntq %%mm0, (%1, %2)\n"
				: 
				: "r" (src), "r" (dst), "r" (i)
				: "memory");
		}
		__asm__ __volatile__ (
			"emms\n");
	}
#else
	memcpy (dst, src, n_bytes);
#endif
}

/*
 * The functions backtrace_* are based on oprofile's backtrace.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon
 * @author David Smith
 */
static void backtrace_warning_symbol(void *data, char *msg,
                                     unsigned long symbol)
{
        /* Ignore warnings */
}

static void backtrace_warning(void *data, char *msg)
{
        /* Ignore warnings */
}

struct backtrace_info_t
{
	SysprofStackTrace *trace;
	int pos;
};

static int backtrace_stack(void *data, char *name)
{
	/* Don't bother with IRQ stacks for now */
	return -1;
}

static void backtrace_address(void *data, unsigned long addr, int reliable)
{
	struct backtrace_info_t *info = data;

	if (info->pos < SYSPROF_MAX_ADDRESSES && reliable)
		info->trace->kernel_stack[info->pos++] = (void *)addr;
}

const static struct stacktrace_ops backtrace_ops = {
        .warning = backtrace_warning,
        .warning_symbol = backtrace_warning_symbol,
        .stack = backtrace_stack,
        .address = backtrace_address,
};

static struct pt_regs *
trace_kernel (struct pt_regs *regs,
	      SysprofStackTrace *trace)
{
	struct backtrace_info_t info;
	char *eos;
	unsigned long stack;
	unsigned long bp;

	trace->kernel_stack[0] = (void *)regs->REG_INS_PTR;
	
	info.trace = trace;
	info.pos = 1;

	stack = (unsigned long) ((char *)regs + sizeof (struct pt_regs));
#ifdef CONFIG_FRAME_POINTER
	/* We can't set it to 0, because then the stack trace will
	 * contain the stack of the timer interrupt
	 */
	bp = regs->REG_FRAME_PTR;
#else
	bp = 0;
#endif
	
	dump_trace(NULL, regs, stack, bp, &backtrace_ops, &info);

	trace->n_kernel_words = info.pos;
	
	/* Now trace the user stack */
	eos = (char *)current->thread.REG_STACK_PTR0 - sizeof (struct pt_regs);
	
	return (struct pt_regs *)eos;
}

static void
framepointer_trace (struct pt_regs *regs, SysprofStackTrace *trace)
{
	void *framepointer;
	int i;
	StackFrame frame;

	i = 1;
	
	framepointer = (void *)regs->REG_FRAME_PTR;
	
	while (read_frame (framepointer, &frame) == 0			&&
	       i < SYSPROF_MAX_ADDRESSES				&&
	       (unsigned long)framepointer >= regs->REG_STACK_PTR)
	{
		trace->addresses[i++] = (void *)frame.return_address;
		framepointer = (StackFrame *)frame.next;
	}
	
	trace->n_addresses = i;
}

static void
heuristic_trace (struct pt_regs *regs,
		 SysprofStackTrace *trace)
{
	unsigned long esp = regs->REG_STACK_PTR;
	unsigned long eos = current->mm->start_stack;

	if (esp < eos - (current->mm->stack_vm << PAGE_SHIFT)) {
		/* Stack pointer is not in stack map */
		printk (KERN_ALERT "too small stackpointer in %d\n", current->pid);
		return;
	}
	
	if (esp < eos) {
#if 0
		printk (KERN_ALERT "ok stackpointer\n");
#endif
		unsigned long i;
		int j;
#if 0
		int n_bytes = minimum (eos - esp, (SYSPROF_MAX_ADDRESSES - 1) * sizeof (void *));
#endif

		j = 1;
		for (i = esp; i < eos && j < SYSPROF_MAX_ADDRESSES; i += sizeof (void *)) {
			unsigned long x;
			struct vm_area_struct *vma;

			if (__copy_from_user_inatomic (&x, (void *)i, sizeof (unsigned long)))
				break;

			vma = find_vma (current->mm, x);
			
			if (vma && vma->vm_flags & VM_EXEC)
				if (vma->vm_start <= x && x <= vma->vm_end)
					trace->addresses[j++] = (void *)x;
		}
		
#if 0
		if (__copy_from_user_inatomic (&(trace->addresses[1]), esp, n_bytes) == 0)
			trace->n_addresses = n_bytes / sizeof (void *);
		else
			trace->n_addresses = 1;
			
		j = 1;
		for (i = esp; i < eos && j < SYSPROF_MAX_ADDRESSES; i += sizeof (void *)) {
			void *x;
			if (__copy_from_user_inatomic (
				    &x, (char *)i, sizeof (x)) == 0) {

				if ((unsigned long)x != 1)
					trace->addresses[j++] = x;
			}
		}
#endif
		
		trace->n_addresses = j;

		return;
	}

#if 0
	printk (KERN_ALERT "too big stackpointer\n");
#endif
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
	SysprofStackTrace *trace = &(area->traces[area->head]);
	int is_user;
	static atomic_t in_timer_notify = ATOMIC_INIT(1);
	int n;

	n = ++get_cpu_var(n_samples);
	put_cpu_var(n_samples);

	if (n % INTERVAL != 0)
		return 0;
	
	/* 0: locked, 1: unlocked */
	if (!atomic_dec_and_test(&in_timer_notify)) {
		goto out;
	}

	is_user = user_mode(regs);

	if (!current || current->pid == 0)
		goto out;
	
	if (is_user && current->state != TASK_RUNNING)
		goto out;

	memset(trace, 0, sizeof (SysprofStackTrace));
	
	trace->pid = current->pid;

	trace->n_kernel_words = 0;
	trace->n_addresses = 0;

	if (!is_user)
		regs = trace_kernel (regs, trace);

	trace->addresses[0] = (void *)regs->REG_INS_PTR;
	trace->n_addresses = 1;

	if (current->mm) {
#ifdef CONFIG_X86_64
		heuristic_trace (regs, trace);
#elif CONFIG_X86
		framepointer_trace (regs, trace);
#else
#error Sysprof only supports the i386 and x86-64 architectures
#endif
	}
	
	if (trace->n_addresses == SYSPROF_MAX_ADDRESSES)
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

static ssize_t
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

