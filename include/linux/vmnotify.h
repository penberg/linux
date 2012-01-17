#ifndef _LINUX_VMNOTIFY_H
#define _LINUX_VMNOTIFY_H

#include <linux/types.h>

enum {
	VMNOTIFY_TYPE_FREE_THRESHOLD	= 1ULL << 0,
	VMNOTIFY_TYPE_SAMPLE		= 1ULL << 1,
};

enum {
	VMNOTIFY_EATTR_NR_AVAIL_PAGES	= 1ULL << 0,
	VMNOTIFY_EATTR_NR_FREE_PAGES	= 1ULL << 1,
	VMNOTIFY_EATTR_NR_SWAP_PAGES	= 1ULL << 2,
};

struct vmnotify_config {
	/*
	 * Size of the struct for ABI extensibility.
	 */
	__u32			size;

	/*
	 * Notification type bitmask
	 */
	__u64			type;

	/*
	 * Attributes that are delivered as part of events.
	 */
	__u64			event_attrs;

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
	/*
	 * Size of the struct for ABI extensibility.
	 */
	__u32			size;

	__u64			attrs;

	__u64			attr_values[];
};

#endif /* _LINUX_VMNOTIFY_H */
