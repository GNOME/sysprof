#include <linux/config.h>
#ifdef CONFIG_SMP
# define __SMP__
#endif
#include <asm/atomic.h>
#include <linux/kernel.h>  /* Needed for KERN_ALERT */
#include <linux/module.h>  /* Needed by all modules */
#include <linux/tqueue.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/poll.h>

#include "sysprof-module.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Soeren Sandmann (sandmann@daimi.au.dk)");

#define SAMPLES_PER_SECOND 50   /* must divide HZ */

static const int cpu_profiler = 0; /* 0: page faults, 1: cpu */

static void on_timer_interrupt (void *);

static struct tq_struct timer_task =
{
    { NULL, NULL },
    0,
    on_timer_interrupt,
    NULL
};

int exiting = 0;
DECLARE_WAIT_QUEUE_HEAD (wait_for_exit);

#define N_TRACES 256

static SysprofStackTrace	stack_traces[N_TRACES];
static SysprofStackTrace *  head = &stack_traces[0];
static SysprofStackTrace *  tail = &stack_traces[0];
DECLARE_WAIT_QUEUE_HEAD (wait_for_trace);

static void
generate_stack_trace (struct task_struct *task,
		      SysprofStackTrace *trace)
{
#define START_OF_STACK 0xBFFFFFFF

    typedef struct StackFrame StackFrame;
    struct StackFrame {
	StackFrame *next;
	void *return_address;
    };

    struct pt_regs *regs = (struct pt_regs *)(
	(long)current + THREAD_SIZE - sizeof (struct pt_regs));
	
    StackFrame *frame;
    int i;

    memset (trace, 0, sizeof (SysprofStackTrace));
    
    trace->pid = current->pid;
    trace->truncated = 0;

    trace->addresses[0] = (void *)regs->eip;
    i = 1;

    frame = (StackFrame *)regs->ebp;

    while (frame && i < SYSPROF_MAX_ADDRESSES &&
	   (long)frame < (long)START_OF_STACK &&
	   (long)frame >= regs->esp)
    {
	void *next = NULL;
	
	if (verify_area(VERIFY_READ, frame, sizeof(StackFrame)) == 0)
	{
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

struct page *
hijacked_nopage (struct vm_area_struct * area,
		 unsigned long address,
		 int unused)
{
    if (current && current->pid != 0)
    {
#if 0
	generate_stack_trace (current, head);
	if (head++ == &stack_traces[N_TRACES - 1])
	    head = &stack_traces[0];

	wake_up (&wait_for_trace);
#endif
	memset (head, 0, sizeof (SysprofStackTrace));

	head->n_addresses = 1;
	head->pid = current->pid;
	head->addresses[0] = (void *)address;
	head->truncated = 0;

	if (area->vm_file)
	{
	    char *line = d_path (area->vm_file->f_dentry,
				 area->vm_file->f_vfsmnt,
				 head->filename, PAGE_SIZE);

	    strncpy (head->filename, line, sizeof (head->filename) - 1);
#if 0

	    if (line == head->filename)
		printk (KERN_ALERT "got name\n");
	    else if (line == NULL)
		printk (KERN_ALERT "noasdf\n");
	    else
		printk (KERN_ALERT "%s\n", line);
#endif
	    head->map_start = area->vm_start;
	}
	
	if (head++ == &stack_traces[N_TRACES - 1])
	    head = &stack_traces[0];

	wake_up (&wait_for_trace);
    }
    
    return filemap_nopage (area, address, unused);   
}

static void
hijack_nopages (struct task_struct *task)
{
    struct mm_struct *mm = task->mm;
    struct vm_area_struct *vma;
    
    if (!mm)
	return;

    for (vma = mm->mmap; vma != NULL; vma = vma->vm_next)
    {
	if (vma->vm_ops && vma->vm_ops->nopage == filemap_nopage)
	    vma->vm_ops->nopage = hijacked_nopage;
    }
}

static void
clean_hijacked_nopages (void)
{
    struct task_struct *task;
    
    for_each_process (task)
    {
	struct mm_struct *mm = task->mm;

	if (mm)
	{
	    struct vm_area_struct *vma;
	    
	    for (vma = mm->mmap; vma != NULL; vma = vma->vm_next)
	    {
		if (vma->vm_ops && vma->vm_ops->nopage == hijacked_nopage)
		    vma->vm_ops->nopage = filemap_nopage;
	    }
	}
    }
}

static void
on_timer_interrupt (void *data)
{
    static int n_ticks = 0;
    
    if (exiting)
    {
	clean_hijacked_nopages ();
	wake_up (&wait_for_exit);
	return;
    }

    ++n_ticks;
    
    if (current && current->pid != 0 && 
	(n_ticks % (HZ / SAMPLES_PER_SECOND)) == 0)
    {
	if (cpu_profiler)
	{
	    generate_stack_trace (current, head);
	    
	    if (head++ == &stack_traces[N_TRACES - 1])
		head = &stack_traces[0];
	    
	    wake_up (&wait_for_trace);
	}
	else
	{
	    hijack_nopages (current);
	}
    }
    
    queue_task (&timer_task, &tq_timer);
}

static int
procfile_read (char   *buffer,
	       char  **buffer_location,
	       off_t   offset,
	       int     buffer_length,
	       int    *eof,
	       void   *data)
{
#if 0
    if (offset > 0)
	return 0;
#endif

    wait_event_interruptible (wait_for_trace, head != tail);
    *buffer_location = (char *)tail;
    if (tail++ == &stack_traces[N_TRACES - 1])
	tail = &stack_traces[0];
    return sizeof (SysprofStackTrace);
}

struct proc_dir_entry *trace_proc_file;

static unsigned int
procfile_poll (struct file *filp, poll_table *poll_table)
{
    if (head != tail)
    {
	return POLLIN;
    }
    poll_wait (filp, &wait_for_trace, poll_table);
    if (head != tail)
    {
	return POLLIN;
    }
    return 0;
}

int
init_module (void)
{
    trace_proc_file =
	create_proc_entry ("sysprof-trace", S_IFREG | S_IRUGO, &proc_root);
    
    if (!trace_proc_file)
	return 1;
    
    trace_proc_file->read_proc = procfile_read;
    trace_proc_file->proc_fops->poll = procfile_poll;
    trace_proc_file->size = sizeof (SysprofStackTrace);
    
    queue_task(&timer_task, &tq_timer);
    
    printk (KERN_ALERT "starting sysprof module\n");

    return 0;
}

void
cleanup_module(void)
{
    exiting = 1;
    sleep_on (&wait_for_exit);
    remove_proc_entry ("sysprof-trace", &proc_root);

    printk (KERN_ALERT "stopping sysprof module\n");
}
