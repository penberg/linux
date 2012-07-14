#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/init_task.h>
#include <linux/mqueue.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/processor.h>   /* INIT_MMAP */


/* This is copied from i386 for now.  I don't know what we'll
   need to change yet.  KPH 2000-04-25 */

static struct fs_struct init_fs = INIT_FS;
static struct files_struct init_files = INIT_FILES;
static struct signal_struct init_signals = INIT_SIGNALS(init_signals);
static struct sighand_struct init_sighand = INIT_SIGHAND(init_sighand);
struct mm_struct init_mm = INIT_MM(init_mm);

/*
 * Initial thread structure.
 *
 * We need to make sure that this is 8192-byte aligned due to the
 * way process stacks are handled. This is done by having a special
 * "init_task" linker map entry..
 *
 * Note that once we drop from IPL 31 to IPL 0 during init, we'll
 * be using the stack inside this union as the kernel stack.
 */
union thread_union init_thread_union
	__attribute__((__section__(".data.init_task"))) =
		{ INIT_THREAD_INFO(init_task) };

/*
 * Initial task structure.
 *
 * All other task structs will be allocated on slabs in fork.c
 */
struct task_struct init_task = INIT_TASK(init_task);

