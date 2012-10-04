#include <linux/anon_inodes.h>
#include <linux/atomic.h>
#include <linux/compiler.h>
#include <linux/vmevent.h>
#include <linux/syscalls.h>
#include <linux/workqueue.h>
#include <linux/file.h>
#include <linux/list.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/swap.h>
#undef nr_swap_pages /* This is defined to a constant for SWAP=n case */

struct vmevent_watch {
	struct vmevent_config		config;

	atomic_t			pending;

	/*
	 * Attributes that are exported as part of delivered VM events.
	 */
	unsigned long			nr_attrs;
	struct vmevent_attr		*sample_attrs;
	struct vmevent_attr		*config_attrs[VMEVENT_CONFIG_MAX_ATTRS];

	/* sampling */
	struct delayed_work		work;

	/* poll */
	wait_queue_head_t		waitq;
};

typedef u64 (*vmevent_attr_sample_fn)(struct vmevent_watch *watch,
				      struct vmevent_attr *attr);

static u64 vmevent_attr_swap_pages(struct vmevent_watch *watch,
				   struct vmevent_attr *attr)
{
#ifdef CONFIG_SWAP
	struct sysinfo si;

	si_swapinfo(&si);

	return si.totalswap;
#else
	return 0;
#endif
}

static u64 vmevent_attr_free_pages(struct vmevent_watch *watch,
				   struct vmevent_attr *attr)
{
	return global_page_state(NR_FREE_PAGES);
}

static u64 vmevent_attr_avail_pages(struct vmevent_watch *watch,
				    struct vmevent_attr *attr)
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

	return fn(watch, attr);
}

enum {
	VMEVENT_ATTR_STATE_VALUE_WAS_LT	= (1UL << 30),
	VMEVENT_ATTR_STATE_VALUE_WAS_GT	= (1UL << 31),
};

static bool vmevent_match_attr(struct vmevent_attr *attr, u64 value)
{
	u32 state = attr->state;
	bool attr_lt = state & VMEVENT_ATTR_STATE_VALUE_LT;
	bool attr_gt = state & VMEVENT_ATTR_STATE_VALUE_GT;
	bool attr_eq = state & VMEVENT_ATTR_STATE_VALUE_EQ;
	bool edge = state & VMEVENT_ATTR_STATE_EDGE_TRIGGER;
	u32 was_lt_mask = VMEVENT_ATTR_STATE_VALUE_WAS_LT;
	u32 was_gt_mask = VMEVENT_ATTR_STATE_VALUE_WAS_GT;
	bool lt = value < attr->value;
	bool gt = value > attr->value;
	bool eq = value == attr->value;
	bool was_lt = state & was_lt_mask;
	bool was_gt = state & was_gt_mask;
	bool was_eq = was_lt && was_gt;
	bool ret = false;

	if (!state)
		return false;

	if (!attr_lt && !attr_gt && !attr_eq)
		return false;

	if (((attr_lt && lt) || (attr_gt && gt) || (attr_eq && eq)) && !edge)
		return true;

	if (attr_eq && eq && was_eq) {
		return false;
	} else if (attr_lt && lt && was_lt && !was_eq) {
		return false;
	} else if (attr_gt && gt && was_gt && !was_eq) {
		return false;
	} else if (eq) {
		state |= was_lt_mask;
		state |= was_gt_mask;
		if (attr_eq)
			ret = true;
	} else if (lt) {
		state |= was_lt_mask;
		state &= ~was_gt_mask;
		if (attr_lt)
			ret = true;
	} else if (gt) {
		state |= was_gt_mask;
		state &= ~was_lt_mask;
		if (attr_gt)
			ret = true;
	}

	attr->state = state;
	return ret;
}

static bool vmevent_match(struct vmevent_watch *watch)
{
	struct vmevent_config *config = &watch->config;
	int i;

	for (i = 0; i < config->counter; i++) {
		struct vmevent_attr *attr = &config->attrs[i];
		u64 val;

		val = vmevent_sample_attr(watch, attr);
		if (vmevent_match_attr(attr, val))
			return true;
	}

	return false;
}

/*
 * This function is called from a workqueue, which can have only one
 * execution thread, so we don't need to worry about racing w/ ourselves.
 *
 * We also don't need to worry about several instances of us accessing
 * the same vmevent_watch, as we allocate vmevent_watch together w/ the
 * work instance in vmevent_fd(), so there is always one work per
 * vmevent_watch.
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

		attr->value = vmevent_sample_attr(watch,
						  watch->config_attrs[i]);
	}

	atomic_set(&watch->pending, 1);
}

static void vmevent_schedule_watch(struct vmevent_watch *watch)
{
	schedule_delayed_work(&watch->work,
		nsecs_to_jiffies64(watch->config.sample_period_ns));
}

static struct vmevent_watch *work_to_vmevent_watch(struct work_struct *work)
{
	struct delayed_work *wk = to_delayed_work(work);

	return container_of(wk, struct vmevent_watch, work);
}

static void vmevent_timer_fn(struct work_struct *work)
{
	struct vmevent_watch *watch = work_to_vmevent_watch(work);

	vmevent_sample(watch);

	if (atomic_read(&watch->pending))
		wake_up(&watch->waitq);

	vmevent_schedule_watch(watch);
}

static void vmevent_start_timer(struct vmevent_watch *watch)
{
	INIT_DELAYED_WORK_DEFERRABLE(&watch->work, vmevent_timer_fn);
	vmevent_schedule_watch(watch);
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

	cancel_delayed_work_sync(&watch->work);

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

		attrs[nr].type = attr->type;
		attrs[nr].value = 0;
		attrs[nr].state = 0;

		watch->config_attrs[nr] = attr;

		nr++;
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
