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

#include "sysprof-module.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Soeren Sandmann (sandmann@daimi.au.dk)");

#define SAMPLES_PER_SECOND (20)
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

static void
generate_stack_trace(struct pt_regs *regs,
		     SysprofStackTrace *trace)
{
#define START_OF_STACK 0xBFFFFFFF 
	
	typedef struct StackFrame StackFrame;
	struct StackFrame {
		StackFrame *next;
		void *return_address;
	};
	
	StackFrame *frame;
	int i;
	
	memset(trace, 0, sizeof (SysprofStackTrace));
	
	trace->pid = current->pid;
	trace->truncated = 0;
	
	trace->addresses[0] = (void *)regs->eip;
	i = 1;
	frame = (StackFrame *)regs->ebp;
	
	while (frame && i < SYSPROF_MAX_ADDRESSES &&
	       (long)frame < (long)START_OF_STACK &&
	       (long)frame >= regs->esp) {
		void *next = NULL;
		
		if (verify_area(VERIFY_READ, frame, sizeof(StackFrame)) == 0) {
			void *return_address;
			
			__get_user (return_address, &(frame->return_address));
			__get_user (next, &(frame->next));
			trace->addresses[i++] = return_address;
			
			if ((long)next <= (long)frame)
				next = NULL;
		}
		
		frame = next;
	}
	
	trace->n_addresses = i;
	if (i == SYSPROF_MAX_ADDRESSES)
		trace->truncated = 1;
	else
		trace->truncated = 0;
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

		struct pt_regs *regs = ((struct pt_regs *) (THREAD_SIZE + (unsigned long) current->thread_info)) - 1;
#if 0
	struct pt_regs *regs = (struct pt_regs *)(
			(long)current + THREAD_SIZE - sizeof (struct pt_regs));
#endif
		
		generate_stack_trace (regs, head);
		
		if (head++ == &stack_traces[N_TRACES - 1])
			head = &stack_traces[0];
		
		wake_up (&wait_for_trace);
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
	if (buffer_len < sizeof (SysprofStackTrace)) 
		return -ENOMEM;
	
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
