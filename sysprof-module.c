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

#define SAMPLES_PER_SECOND (30)
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
static struct timer_list timer;

static void
init_timeout (void)
{
	timer.function = on_timer;
	init_timer(&timer);
}

static void
remove_timeout(void)
{
	del_timer (&timer);
}

static void
add_timeout(unsigned int interval,
	    TimeoutFunc  f)
{
	mod_timer(&timer, jiffies + INTERVAL);
}

typedef struct userspace_reader userspace_reader;
struct userspace_reader
{
	struct task_struct *task;
	unsigned long user_page;
	unsigned long kernel_page;
	struct page *page;
};

static void
init_userspace_reader (userspace_reader *reader,
		       struct task_struct *task)
{
	reader->task = task;
	reader->user_page = 0;
	reader->kernel_page = 0;
	reader->page = NULL;
}

static int
read_user_space (userspace_reader *reader,
		 unsigned long address,
		 unsigned long *result)
{
	unsigned long user_page = (address & PAGE_MASK);
	int res;

	if (user_page == 0)
		return 0;

	if (!reader->user_page || user_page != reader->user_page) {
		int found;
		struct page *page;
		
		found = get_user_pages (reader->task, reader->task->mm, user_page,
					1, 0, 0, &page, NULL);

		if (!found)
			return 0;

		if (reader->user_page)
			kunmap (reader->page);

		reader->user_page = user_page;
		reader->kernel_page = (unsigned long)kmap (page);
		reader->page = page;
	}

	if (get_user (res, (int *)(reader->kernel_page + (address - user_page))) != 0)
		return 0;

	*result = res;
	
	return 1;
}

static void
done_userspace_reader (userspace_reader *reader)
{
	if (reader->user_page)
		kunmap (reader->page);
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
#ifdef NOT_ON_4G4G	/* FIXME: What is the symbol really called? */
#  define START_OF_STACK 0xBFFFFFFF
#else
#  define START_OF_STACK 0xFF000000
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

	init_userspace_reader (&reader, task);
	while (i < SYSPROF_MAX_ADDRESSES && read_frame (&reader, addr, &frame)  &&
	       addr < START_OF_STACK && addr >= regs->esp) {
		trace->addresses[i++] = (void *)frame.return_address;
		addr = frame.next;
	}
	done_userspace_reader (&reader);
	
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
queue_generate_stack_trace (struct task_struct *cur)
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
	task_t *task = current;

	++n_ticks;

	if (task && task->pid != 0)
		queue_generate_stack_trace (task);
	
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
