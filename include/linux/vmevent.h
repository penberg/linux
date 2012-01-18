#ifndef _LINUX_VMEVENT_H
#define _LINUX_VMEVENT_H

#include <linux/types.h>

enum {
	VMEVENT_TYPE_FREE_THRESHOLD	= 1ULL << 0,
	VMEVENT_TYPE_SAMPLE		= 1ULL << 1,
};

enum {
	VMEVENT_EATTR_NR_AVAIL_PAGES	= 1ULL << 0,
	VMEVENT_EATTR_NR_FREE_PAGES	= 1ULL << 1,
	VMEVENT_EATTR_NR_SWAP_PAGES	= 1ULL << 2,
};

struct vmevent_config {
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
	 * Threshold of free pages in the system.
	 */
	__u32			free_pages_threshold;

	/*
	 * Sample period in nanoseconds
	 */
	__u64			sample_period_ns;
};

struct vmevent_event {
	/*
	 * Size of the struct for ABI extensibility.
	 */
	__u32			size;

	__u64			attrs;

	__u64			attr_values[];
};

#endif /* _LINUX_VMEVENT_H */
