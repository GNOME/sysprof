/* -*- c-basic-offset: 8 -*- */

#include <linux/config.h>
#ifdef CONFIG_SMP
# define __SMP__
#endif
#include <asm/atomic.h>
#include <linux/kernel.h>  /* Needed for KERN_ALERT */
#include <linux/module.h>  /* Needed by all modules */
#ifdef KERNEL24
#  include <linux/tqueue.h>
#endif
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/poll.h>
#include <linux/highmem.h>

#include "sysprof-module.h"

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

typedef void (* TimeoutFunc)(unsigned long data);
static void on_timer(unsigned long);

/*
 * Wrappers to cover up kernel differences in timer handling
 */
#ifdef KERNEL24
static struct tq_struct timer_task =
{
	{ NULL, NULL },
	0,
	on_timer,
	NULL
};

int exiting = 0;
#else

static struct timer_list timer;

#endif

static void
init_timeout (void)
{
#ifdef KERNEL24
	/* no setup needed */
#else
	init_timer(&timer);
	timer.function = on_timer;
#endif
}

static void
remove_timeout(void)
{
#ifdef KERNEL24
	exiting = 1;
	sleep_on (&wait_for_exit);
#else
	del_timer (&timer);
#endif
}

static void
add_timeout(unsigned int interval,
	    TimeoutFunc  f)
{
#ifdef KERNEL24
	queue_task(&timer_task, &tq_timer);
#else
	mod_timer(&timer, jiffies + INTERVAL);
#endif
}

/*
 * The portable part of the driver starts here
 */

static int
read_task_address (struct task_struct *task, unsigned long address, int *result)
{
	unsigned long page_addr = (address & PAGE_MASK);
	int found;
	struct page *page;
	void *kaddr;
	int res;

	if (!task || !task->mm)
		return 0;

	found = get_user_pages (task, task->mm, page_addr, 1, 0, 0, &page, NULL);
	if (!found)
		return 0;
	
	kaddr = kmap_atomic (page, KM_SOFTIRQ0);

	if (get_user (res, (int *)kaddr + (address - page_addr) / 4)) {
		kunmap_atomic (page, KM_SOFTIRQ0);
		return 0;
	}
	       
	kunmap_atomic (page, KM_SOFTIRQ0);
	
	*result = res;
	
	return 1;
}

typedef struct StackFrame StackFrame;
struct StackFrame {
	unsigned long next;
	unsigned long return_address;
};

static int
read_frame (struct task_struct *task, unsigned long addr, StackFrame *frame)
{
	if (!addr || !frame)
		return 0;

	frame->next = 0;
	frame->return_address = 0;
	
	if (!read_task_address (task, addr, (int *)&(frame->next)))
		return 0;

	if (!read_task_address (task, addr + 4, (int *)&(frame->return_address)))
		return 0;

	return 1;
}

static void
generate_stack_trace(struct task_struct *task,
		     SysprofStackTrace *trace)
{
#ifdef NOT_ON_4G4G	/* FIXME: What is the symbol really called? */
#  define START_OF_STACK 0xBFFFFFFF
#else
#  define START_OF_STACK 0xFF000000
#endif
	struct pt_regs *regs = ((struct pt_regs *) (THREAD_SIZE + (unsigned long) task->thread_info)) - 1;
	StackFrame frame;
	unsigned long addr;
	int i;
	
	memset(trace, 0, sizeof (SysprofStackTrace));
	
	trace->pid = task->pid;
	trace->truncated = 0;
	
	trace->addresses[0] = (void *)regs->eip;

	i = 1;

	addr = regs->ebp;

#if 0
	printk (KERN_ALERT "in generate\n");
#endif
#if 0
	read_frame (task, addr, &frame);
#endif
	while (i < SYSPROF_MAX_ADDRESSES && read_frame (task, addr, &frame)) {
		trace->addresses[i++] = (void *)frame.return_address;
		
		addr = frame.next;
	}

#if 0
	printk (KERN_ALERT "done (frame.next = %p)\n", frame.next);
#endif
	       
#if 0
	
	read_frame (task, regs->ebp, &frame);
	
	while (i < SYSPROF_MAX_ADDRESSES &&
	       (long)frame < (long)START_OF_STACK &&
	       (long)frame >= regs->esp) {
		void *next = NULL;

		printk(KERN_ALERT "esp: %lx\n", regs->esp);
		printk(KERN_ALERT "ebp: %lx\n", regs->ebp);
		
		if (verify_area(VERIFY_READ, frame, sizeof(StackFrame)) == 0) {
			void *return_address;
			
			read_task_address (task, (unsigned long)return_address, (int *)&(frame->return_address));
			read_task_address (task, (unsigned long)next, (int *)&(frame->next));
					   
			trace->addresses[i++] = return_address;
			
			if ((long)next <= (long)frame)
				next = NULL;
			else
				printk(KERN_ALERT "bad next\n");
		}
		else
			printk(KERN_ALERT "couldn't verify\n");
		
		frame = next;
		if (frame == NULL)
			printk(KERN_ALERT "frame %d is null\n", i);
	}
#endif
	
	trace->n_addresses = i;
	if (i == SYSPROF_MAX_ADDRESSES)
		trace->truncated = 1;
	else
		trace->truncated = 0;
}

struct work_struct work;
static int in_queue;

static void
do_generate (void *data)
{
	struct task_struct *task = data;
	struct task_struct *p;

	in_queue = 0;
	
	/* Make sure the task still exists */
	for_each_process (p)
		if (p == task)
			goto go_ahead;
	return;
	
 go_ahead:
	generate_stack_trace(task, head);
	if (head++ == &stack_traces[N_TRACES - 1])
		head = &stack_traces[0];
			     
	wake_up (&wait_for_trace);
}

static void
queue_stack (struct task_struct *cur)
{
	if (in_queue)
		return;

	in_queue = 1;
	
	INIT_WORK (&work, do_generate, cur);

	schedule_work (&work);
}

static void
on_timer(unsigned long dong)
{
	static int n_ticks = 0;

#ifdef KERNEL24
	if (exiting) {
		wake_up (&wait_for_exit);
		return;
	}
#endif
	
	++n_ticks;

	if (
#ifdef KERNEL24
		current && current->pid != 0 &&
		(n_ticks % (HZ / SAMPLES_PER_SECOND)) == 0
#else
		current && current->pid != 0
#endif
		)
	{
		
#ifdef KERNEL24
		struct pt_regs *regs = (struct pt_regs *)(
			(long)current + THREAD_SIZE - sizeof (struct pt_regs));
#endif
		queue_stack (current);
	}
	
	add_timeout (INTERVAL, on_timer);
}

static int
procfile_read(char *buffer, 
	      char **buffer_location, 
	      off_t offset, 
	      int buffer_len,
	      int *eof,
	      void *data)
{
#if 0
	if (buffer_len < sizeof (SysprofStackTrace)) 
		return -ENOMEM;
#endif
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
	
	init_timeout();
	
	add_timeout(INTERVAL, on_timer);
	
	printk(KERN_ALERT "starting sysprof module\n");
	
	return 0;
}

void
cleanup_module(void)
{
	remove_timeout();
	
	remove_proc_entry("sysprof-trace", &proc_root);
	
	printk(KERN_ALERT "stopping sysprof module\n");
}
