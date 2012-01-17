#include <linux/anon_inodes.h>
#include <linux/vmnotify.h>
#include <linux/syscalls.h>
#include <linux/file.h>
#include <linux/list.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/swap.h>

#define VMNOTIFY_MAX_FREE_THRESHOD	100

struct vmnotify_watch {
	struct vmnotify_config		config;

	struct mutex			mutex;
	bool				pending;
	struct vmnotify_event		event;

	/* sampling */
	struct hrtimer			timer;

	/* poll */
	wait_queue_head_t		waitq;
};

static bool vmnotify_match(struct vmnotify_watch *watch, struct vmnotify_event *event)
{
	if (watch->config.type & VMNOTIFY_TYPE_FREE_THRESHOLD) {
		u64 threshold;

		if (!event->nr_avail_pages)
			return false;

		threshold = event->nr_free_pages * 100 / event->nr_avail_pages;
		if (threshold > watch->config.free_threshold)
			return false;
	}

	return true;
}

static void vmnotify_sample(struct vmnotify_watch *watch)
{
	struct vmnotify_event event;
	struct sysinfo si;

	memset(&event, 0, sizeof(event));

	event.size		= sizeof(event);
	event.nr_free_pages	= global_page_state(NR_FREE_PAGES);

	si_meminfo(&si);
	event.nr_avail_pages	= si.totalram;

#ifdef CONFIG_SWAP
	si_swapinfo(&si);
	event.nr_swap_pages	= si.totalswap;
#endif

	if (!vmnotify_match(watch, &event))
		return;

	mutex_lock(&watch->mutex);

	watch->pending = true;

	memcpy(&watch->event, &event, sizeof(event));

	mutex_unlock(&watch->mutex);
}

static enum hrtimer_restart vmnotify_timer_fn(struct hrtimer *hrtimer)
{
	struct vmnotify_watch *watch = container_of(hrtimer, struct vmnotify_watch, timer);
	u64 sample_period = watch->config.sample_period_ns;

	vmnotify_sample(watch);

	hrtimer_forward_now(hrtimer, ns_to_ktime(sample_period));

	wake_up(&watch->waitq);

	return HRTIMER_RESTART;
}

static void vmnotify_start_timer(struct vmnotify_watch *watch)
{
	u64 sample_period = watch->config.sample_period_ns;

	hrtimer_init(&watch->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	watch->timer.function = vmnotify_timer_fn;

	hrtimer_start(&watch->timer, ns_to_ktime(sample_period), HRTIMER_MODE_REL_PINNED);
}

static unsigned int vmnotify_poll(struct file *file, poll_table *wait)
{
	struct vmnotify_watch *watch = file->private_data;
	unsigned int events = 0;

	poll_wait(file, &watch->waitq, wait);

	mutex_lock(&watch->mutex);

	if (watch->pending)
		events |= POLLIN;

	mutex_unlock(&watch->mutex);

	return events;
}

static ssize_t vmnotify_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct vmnotify_watch *watch = file->private_data;
	int ret = 0;

	mutex_lock(&watch->mutex);

	if (!watch->pending)
		goto out_unlock;

	if (copy_to_user(buf, &watch->event, sizeof(struct vmnotify_event))) {
		ret = -EFAULT;
		goto out_unlock;
	}

	ret = watch->event.size;

	watch->pending = false;

out_unlock:
	mutex_unlock(&watch->mutex);

	return ret;
}

static int vmnotify_release(struct inode *inode, struct file *file)
{
	struct vmnotify_watch *watch = file->private_data;

	hrtimer_cancel(&watch->timer);

	kfree(watch);

	return 0;
}

static const struct file_operations vmnotify_fops = {
	.poll		= vmnotify_poll,
	.read		= vmnotify_read,
	.release	= vmnotify_release,
};

static struct vmnotify_watch *vmnotify_watch_alloc(void)
{
	struct vmnotify_watch *watch;

	watch = kzalloc(sizeof *watch, GFP_KERNEL);
	if (!watch)
		return NULL;

	mutex_init(&watch->mutex);

	init_waitqueue_head(&watch->waitq);

	return watch;
}

static int vmnotify_copy_config(struct vmnotify_config __user *uconfig,
				struct vmnotify_config *config)
{
	int ret;

	ret = copy_from_user(config, uconfig, sizeof(struct vmnotify_config));
	if (ret)
		return -EFAULT;

	if (!config->type)
		return -EINVAL;

	if (config->type & VMNOTIFY_TYPE_SAMPLE) {
		if (config->sample_period_ns < NSEC_PER_MSEC)
			return -EINVAL;
	}

	if (config->type & VMNOTIFY_TYPE_FREE_THRESHOLD) {
		if (config->free_threshold > VMNOTIFY_MAX_FREE_THRESHOD)
			return -EINVAL;
	}

	return 0;
}

SYSCALL_DEFINE1(vmnotify_fd,
		struct vmnotify_config __user *, uconfig)
{
	struct vmnotify_watch *watch;
	struct file *file;
	int err;
	int fd;

	watch = vmnotify_watch_alloc();
	if (!watch)
		return -ENOMEM;

	err = vmnotify_copy_config(uconfig, &watch->config);
	if (err)
		goto err_free;

	fd = get_unused_fd_flags(O_RDONLY);
	if (fd < 0) {
		err = fd;
		goto err_free;
	}

	file = anon_inode_getfile("[vmnotify]", &vmnotify_fops, watch, O_RDONLY);
	if (IS_ERR(file)) {
		err = PTR_ERR(file);
		goto err_fd;
	}

	fd_install(fd, file);

	if (watch->config.type & VMNOTIFY_TYPE_SAMPLE)
		vmnotify_start_timer(watch);

	return fd;

err_fd:
	put_unused_fd(fd);
err_free:
	kfree(watch);
	return err;
}
