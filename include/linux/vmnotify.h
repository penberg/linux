#ifndef _LINUX_VMNOTIFY_H
#define _LINUX_VMNOTIFY_H

#include <linux/types.h>

enum {
	VMNOTIFY_TYPE_FREE_THRESHOLD	= 1ULL << 0,
	VMNOTIFY_TYPE_SAMPLE		= 1ULL << 1,
};

struct vmnotify_config {
	/*
	 * Size of the struct for ABI extensibility.
	 */
	__u32		   size;

	/*
	 * Notification type bitmask
	 */
	__u64			type;

	/*
	 * Free memory threshold in percentages [1..99]
	 */
	__u32			free_threshold;

	/*
	 * Sample period in nanoseconds
	 */
	__u64			sample_period_ns;
};

struct vmnotify_event {
	/* Size of the struct for ABI extensibility. */
	__u32			size;

	__u64			nr_avail_pages;

	__u64			nr_swap_pages;

	__u64			nr_free_pages;
};

#endif /* _LINUX_VMNOTIFY_H */
