#include <linux/anon_inodes.h>
#include <linux/atomic.h>
#include <linux/vmevent.h>
#include <linux/syscalls.h>
#include <linux/timer.h>
#include <linux/file.h>
#include <linux/list.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/swap.h>
#undef nr_swap_pages /* This is defined to a constant for SWAP=n case */

#define VMEVENT_MAX_FREE_THRESHOD	100

#define VMEVENT_MAX_EATTR_ATTRS	64

struct vmevent_watch_event {
	u64				nr_avail_pages;
	u64				nr_free_pages;
	u64				nr_swap_pages;
};

struct vmevent_watch {
	struct vmevent_config		config;

	atomic_t			pending;

	/*
	 * Attributes that are exported as part of delivered VM events.
	 */
	unsigned long			nr_attrs;
	struct vmevent_attr		*sample_attrs;

	/* sampling */
	struct timer_list		timer;

	/* poll */
	wait_queue_head_t		waitq;
};

typedef u64 (*vmevent_attr_sample_fn)(struct vmevent_watch *watch);

static u64 vmevent_attr_swap_pages(struct vmevent_watch *watch)
{
#ifdef CONFIG_SWAP
	struct sysinfo si;

	si_swapinfo(&si);

	return si.totalswap;
#else
	return 0;
#endif
}

static u64 vmevent_attr_free_pages(struct vmevent_watch *watch)
{
	return global_page_state(NR_FREE_PAGES);
}

static u64 vmevent_attr_avail_pages(struct vmevent_watch *watch)
{
	return totalram_pages;
}

static vmevent_attr_sample_fn attr_samplers[] = {
	[VMEVENT_ATTR_NR_AVAIL_PAGES]   = vmevent_attr_avail_pages,
	[VMEVENT_ATTR_NR_FREE_PAGES]    = vmevent_attr_free_pages,
	[VMEVENT_ATTR_NR_SWAP_PAGES]    = vmevent_attr_swap_pages,
};

static u64 vmevent_sample_attr(struct vmevent_watch *watch, struct vmevent_attr *attr)
{
	vmevent_attr_sample_fn fn = attr_samplers[attr->type];

	return fn(watch);
}

static bool vmevent_match(struct vmevent_watch *watch)
{
	struct vmevent_config *config = &watch->config;
	int i;

	for (i = 0; i < config->counter; i++) {
		struct vmevent_attr *attr = &config->attrs[i];
		u64 value;

		if (!attr->state)
			continue;

		value = vmevent_sample_attr(watch, attr);

		if (attr->state & VMEVENT_ATTR_STATE_VALUE_LT) {
			if (value < attr->value)
				return true;
		}
	}

	return false;
}

/*
 * This function is called from the timer context, which has the same
 * guaranties as an interrupt handler: it can have only one execution
 * thread (unlike bare softirq handler), so we don't need to worry
 * about racing w/ ourselves.
 *
 * We also don't need to worry about several instances of timers
 * accessing the same vmevent_watch, as we allocate vmevent_watch
 * together w/ the timer instance in vmevent_fd(), so there is always
 * one timer per vmevent_watch.
 *
 * All the above makes it possible to implement the lock-free logic,
 * using just the atomic watch->pending variable.
 */
static void vmevent_sample(struct vmevent_watch *watch)
{
	int i;

	if (atomic_read(&watch->pending))
		return;
	if (!vmevent_match(watch))
		return;

	for (i = 0; i < watch->nr_attrs; i++) {
		struct vmevent_attr *attr = &watch->sample_attrs[i];

		attr->value = vmevent_sample_attr(watch, attr);
	}

	atomic_set(&watch->pending, 1);
}

static void vmevent_timer_fn(unsigned long data)
{
	struct vmevent_watch *watch = (struct vmevent_watch *)data;

	vmevent_sample(watch);

	if (atomic_read(&watch->pending))
		wake_up(&watch->waitq);
	mod_timer(&watch->timer, jiffies +
			nsecs_to_jiffies64(watch->config.sample_period_ns));
}

static void vmevent_start_timer(struct vmevent_watch *watch)
{
	init_timer_deferrable(&watch->timer);
	watch->timer.data = (unsigned long)watch;
	watch->timer.function = vmevent_timer_fn;
	watch->timer.expires = jiffies +
			nsecs_to_jiffies64(watch->config.sample_period_ns);
	add_timer(&watch->timer);
}

static unsigned int vmevent_poll(struct file *file, poll_table *wait)
{
	struct vmevent_watch *watch = file->private_data;
	unsigned int events = 0;

	poll_wait(file, &watch->waitq, wait);

	if (atomic_read(&watch->pending))
		events |= POLLIN;

	return events;
}

static ssize_t vmevent_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct vmevent_watch *watch = file->private_data;
	struct vmevent_event *event;
	ssize_t ret = 0;
	u32 size;
	int i;

	size = sizeof(*event) + watch->nr_attrs * sizeof(struct vmevent_attr);

	if (count < size)
		return -EINVAL;

	if (!atomic_read(&watch->pending))
		goto out;

	event = kmalloc(size, GFP_KERNEL);
	if (!event) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < watch->nr_attrs; i++) {
		memcpy(&event->attrs[i], &watch->sample_attrs[i], sizeof(struct vmevent_attr));
	}

	event->counter = watch->nr_attrs;

	if (copy_to_user(buf, event, size)) {
		ret = -EFAULT;
		goto out_free;
	}

	ret = count;

	atomic_set(&watch->pending, 0);
out_free:
	kfree(event);
out:
	return ret;
}

static int vmevent_release(struct inode *inode, struct file *file)
{
	struct vmevent_watch *watch = file->private_data;

	del_timer_sync(&watch->timer);

	kfree(watch);

	return 0;
}

static const struct file_operations vmevent_fops = {
	.poll		= vmevent_poll,
	.read		= vmevent_read,
	.release	= vmevent_release,
};

static struct vmevent_watch *vmevent_watch_alloc(void)
{
	struct vmevent_watch *watch;

	watch = kzalloc(sizeof *watch, GFP_KERNEL);
	if (!watch)
		return NULL;

	init_waitqueue_head(&watch->waitq);

	return watch;
}

static int vmevent_setup_watch(struct vmevent_watch *watch)
{
	struct vmevent_config *config = &watch->config;
	struct vmevent_attr *attrs = NULL;
	unsigned long nr;
	int i;

	nr = 0;

	for (i = 0; i < config->counter; i++) {
		struct vmevent_attr *attr = &config->attrs[i];
		size_t size;
		void *new;

		if (attr->type >= VMEVENT_ATTR_MAX)
			continue;

		size = sizeof(struct vmevent_attr) * (nr + 1);

		new = krealloc(attrs, size, GFP_KERNEL);
		if (!new) {
			kfree(attrs);
			return -ENOMEM;
		}

		attrs = new;

		attrs[nr++].type = attr->type;
	}

	watch->sample_attrs	= attrs;
	watch->nr_attrs		= nr;

	return 0;
}

static int vmevent_copy_config(struct vmevent_config __user *uconfig,
				struct vmevent_config *config)
{
	int ret;

	ret = copy_from_user(config, uconfig, sizeof(struct vmevent_config));
	if (ret)
		return -EFAULT;

	return 0;
}

SYSCALL_DEFINE1(vmevent_fd,
		struct vmevent_config __user *, uconfig)
{
	struct vmevent_watch *watch;
	struct file *file;
	int err;
	int fd;

	watch = vmevent_watch_alloc();
	if (!watch)
		return -ENOMEM;

	err = vmevent_copy_config(uconfig, &watch->config);
	if (err)
		goto err_free;

	err = vmevent_setup_watch(watch);
	if (err)
		goto err_free;

	fd = get_unused_fd_flags(O_RDONLY);
	if (fd < 0) {
		err = fd;
		goto err_free;
	}

	file = anon_inode_getfile("[vmevent]", &vmevent_fops, watch, O_RDONLY);
	if (IS_ERR(file)) {
		err = PTR_ERR(file);
		goto err_fd;
	}

	fd_install(fd, file);

	vmevent_start_timer(watch);

	return fd;

err_fd:
	put_unused_fd(fd);
err_free:
	kfree(watch);
	return err;
}
