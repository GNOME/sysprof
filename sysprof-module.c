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
#include <asm/unistd.h>
#include <linux/pagemap.h>

#include "sysprof-module.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Soeren Sandmann (sandmann@daimi.au.dk)");

#define SAMPLES_PER_SECOND 50   /* must divide HZ */

static const int cpu_profiler = 1; /* 0: page faults, 1: cpu */

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

    while (frame && i < SYSPROF_MAX_ADDRESSES - 1 &&
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

static int
disk_access (struct vm_area_struct *area,
	     unsigned long address)
{
    struct file *file = area->vm_file;
    struct address_space *mapping = file->f_dentry->d_inode->i_mapping;
    struct inode *inode = mapping->host;
    struct page *page, **hash;
    unsigned long size, pgoff, endoff;
    int disk_access = 1;
    
    pgoff = ((address - area->vm_start) >> PAGE_CACHE_SHIFT) + area->vm_pgoff;
    endoff = ((area->vm_end - area->vm_start) >> PAGE_CACHE_SHIFT) + area->vm_pgoff;
    
    /*
     * An external ptracer can access pages that normally aren't
     * accessible..
     */
    size = (inode->i_size + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
    if ((pgoff >= size) && (area->vm_mm == current->mm))
	return 0;
    
    /* The "size" of the file, as far as mmap is concerned, isn't bigger than the mapping */
    if (size > endoff)
	size = endoff;
    
    /*
     * Do we have something in the page cache already?
     */
    hash = page_hash(mapping, pgoff);
    
    page = __find_get_page(mapping, pgoff, hash);
    if (page)
	/*
	 * Ok, found a page in the page cache, now we need to check
	 * that it's up-to-date.
	 */
	if (Page_Uptodate(page))
	    disk_access = 0;

    return disk_access;
}

struct page *
hijacked_nopage (struct vm_area_struct * area,
		 unsigned long address,
		 int unused)
{
    struct page *result;
    int disk;

    disk = disk_access (area, address);

    printk (KERN_ALERT "disk access: %d\n", disk);
    
    result = filemap_nopage (area, address, unused);

    if (current && disk)
    {
	generate_stack_trace (current, head);

#if 0
	memset (head, 0, sizeof (SysprofStackTrace));
    
 	head->pid = current->pid;
 	head->addresses[0] = (void *)address;
	head->truncated = 0;
	head->n_addresses = 1;
#endif

	memmove (&(head->addresses[1]), &(head->addresses[0]),
		 head->n_addresses * 4);
	head->n_addresses++;
	head->addresses[0] = address;
	
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

    return result;
}

static void
hijack_nopage (struct vm_area_struct *vma)
{
    if (vma->vm_ops && vma->vm_ops->nopage == filemap_nopage)
	vma->vm_ops->nopage = hijacked_nopage;
}

static void
hijack_nopages (struct task_struct *task)
{
    struct mm_struct *mm = task->mm;
    struct vm_area_struct *vma;
    
    if (!mm)
	return;

    for (vma = mm->mmap; vma != NULL; vma = vma->vm_next)
	hijack_nopage (vma);
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

struct mmap_arg_struct {
	unsigned long addr;
	unsigned long len;
	unsigned long prot;
	unsigned long flags;
	unsigned long fd;
	unsigned long offset;
};

typedef asmlinkage int (* old_mmap_func) (struct mmap_arg_struct *arg);
typedef asmlinkage long (* mmap2_func) (
    unsigned long addr, unsigned long len,
    unsigned long prot, unsigned long flags,
    unsigned long fd, unsigned long pgoff);

static mmap2_func orig_mmap2;
static old_mmap_func orig_mmap;

static void
after_mmap (long res, unsigned long len)
{
    struct vm_area_struct *vma;
    unsigned long start;

    if (res == -1 || !current || current->pid == 0)
	return;

    start = res;
    
    vma = find_vma (current->mm, start);

    if (vma && vma->vm_end >= start + len)
	hijack_nopage (vma);
#if 0
    else if (vma)
    {
	printk (KERN_ALERT "nope: vm_start: %x, vm_end: %x\n"
		"start: %p, len: %x, start + len: %x\n",
		vma->vm_start, vma->vm_end, start, len, start + len);
    }
    else
	printk (KERN_ALERT "no vma\n");
#endif
    
}

static long
new_mmap2 (unsigned long addr, unsigned long len,
	   unsigned long prot, unsigned long flags,
	   unsigned long fd,   unsigned long pgoff)
{
    int res = orig_mmap2 (addr, len, prot, flags, fd, pgoff);

    after_mmap (res, len);

    return res;
}

static int
new_mmap (struct mmap_arg_struct *arg)
{
    int res = orig_mmap (arg);

    after_mmap (res, arg->len);

    return res;
}

void **sys_call_table = (void **)0xc0347df0;

static void
hijack_mmaps (void)
{
    orig_mmap2 = sys_call_table[__NR_mmap2];
    sys_call_table[__NR_mmap2] = new_mmap2;
    
    orig_mmap = sys_call_table[__NR_mmap];
    sys_call_table[__NR_mmap] = new_mmap;
}

static void
restore_mmaps (void)
{
    sys_call_table[__NR_mmap2] = orig_mmap2;
    sys_call_table[__NR_mmap] = orig_mmap;
}

static void
on_timer_interrupt (void *data)
{
    static int n_ticks = 0;
    
    if (exiting)
    {
#if 0
	if (!cpu_profiler)
	{
	    restore_mmaps();
	    
	    clean_hijacked_nopages ();
	}
#endif
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
#if 0
	    hijack_nopages (current);
#endif
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
    struct task_struct *task;
    trace_proc_file =
	create_proc_entry ("sysprof-trace", S_IFREG | S_IRUGO, &proc_root);
    
    if (!trace_proc_file)
	return 1;
    
    trace_proc_file->read_proc = procfile_read;
    trace_proc_file->proc_fops->poll = procfile_poll;
    trace_proc_file->size = sizeof (SysprofStackTrace);

#if 0
    if (!cpu_profiler)
    {
	hijack_mmaps ();
	
	for_each_process (task)
	    {
		hijack_nopages (task);
	    }
    }
#endif
    
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
