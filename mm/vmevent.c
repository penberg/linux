#include <linux/anon_inodes.h>
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

	struct mutex			mutex;
	bool				pending;

	/*
 	 * Attributes
 	 */
	unsigned long			nr_attrs;
	u64				attr_values[64];

	/* sampling */
	struct timer_list		timer;

	/* poll */
	wait_queue_head_t		waitq;
};

static bool vmevent_match(struct vmevent_watch *watch,
			   struct vmevent_watch_event *event)
{
	if (watch->config.type & VMEVENT_TYPE_FREE_THRESHOLD) {
		if (event->nr_free_pages > watch->config.free_pages_threshold)
			return false;
	}

	return true;
}

static void vmevent_sample(struct vmevent_watch *watch)
{
	struct vmevent_watch_event event;
	int n = 0;

	memset(&event, 0, sizeof(event));

	event.nr_free_pages	= global_page_state(NR_FREE_PAGES);

	event.nr_avail_pages	= totalram_pages;

#ifdef CONFIG_SWAP
	if (watch->config.event_attrs & VMEVENT_EATTR_NR_SWAP_PAGES) {
		struct sysinfo si;

		si_swapinfo(&si);
		event.nr_swap_pages	= si.totalswap;
	}
#endif

	if (!vmevent_match(watch, &event))
		return;

	mutex_lock(&watch->mutex);

	watch->pending = true;

	if (watch->config.event_attrs & VMEVENT_EATTR_NR_AVAIL_PAGES)
		watch->attr_values[n++] = event.nr_avail_pages;

	if (watch->config.event_attrs & VMEVENT_EATTR_NR_FREE_PAGES)
		watch->attr_values[n++] = event.nr_free_pages;

	if (watch->config.event_attrs & VMEVENT_EATTR_NR_SWAP_PAGES)
		watch->attr_values[n++] = event.nr_swap_pages;

	watch->nr_attrs = n;

	mutex_unlock(&watch->mutex);
}

static void vmevent_timer_fn(unsigned long data)
{
	struct vmevent_watch *watch = (struct vmevent_watch *)data;

	vmevent_sample(watch);

	if (watch->pending)
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

	mutex_lock(&watch->mutex);

	if (watch->pending)
		events |= POLLIN;

	mutex_unlock(&watch->mutex);

	return events;
}

static ssize_t vmevent_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct vmevent_watch *watch = file->private_data;
	struct vmevent_event event;
	ssize_t ret = 0;
	u64 attr_size;

	mutex_lock(&watch->mutex);

	if (!watch->pending)
		goto out_unlock;

	attr_size = watch->nr_attrs * sizeof(u64);

	memset(&event, 0, sizeof(event));
	event.size	= sizeof(struct vmevent_event) + attr_size;
	event.attrs	= watch->config.event_attrs;

	if (count < sizeof(event))
		goto out_unlock;

	if (copy_to_user(buf, &event, sizeof(event))) {
		ret = -EFAULT;
		goto out_unlock;
	}

	count -= sizeof(event);

	if (count > attr_size)
		count = attr_size;

	if (copy_to_user(buf + sizeof(event), watch->attr_values, count)) {
		ret = -EFAULT;
		goto out_unlock;
	}

	ret = count;

	watch->pending = false;

out_unlock:
	mutex_unlock(&watch->mutex);

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

	mutex_init(&watch->mutex);

	init_waitqueue_head(&watch->waitq);

	return watch;
}

static int vmevent_copy_config(struct vmevent_config __user *uconfig,
				struct vmevent_config *config)
{
	int ret;

	ret = copy_from_user(config, uconfig, sizeof(struct vmevent_config));
	if (ret)
		return -EFAULT;

	if (!config->type)
		return -EINVAL;

	if (config->type & VMEVENT_TYPE_SAMPLE) {
		if (config->sample_period_ns < NSEC_PER_MSEC)
			return -EINVAL;
	}

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

	if (watch->config.type & VMEVENT_TYPE_SAMPLE)
		vmevent_start_timer(watch);

	return fd;

err_fd:
	put_unused_fd(fd);
err_free:
	kfree(watch);
	return err;
}
