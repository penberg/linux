#ifndef _LINUX_VMEVENT_H
#define _LINUX_VMEVENT_H

#include <linux/types.h>

/*
 * Types of memory attributes which could be monitored through vmevent API
 */
enum {
	VMEVENT_ATTR_NR_AVAIL_PAGES	= 1UL,
	VMEVENT_ATTR_NR_FREE_PAGES	= 2UL,
	VMEVENT_ATTR_NR_SWAP_PAGES	= 3UL,

	VMEVENT_ATTR_MAX		/* non-ABI */
};

/*
 * Attribute state bits for threshold
 */
enum {
	/*
	 * Sample value is less than user-specified value
	 */
	VMEVENT_ATTR_STATE_VALUE_LT	= (1UL << 0),
	/*
	 * Sample value is greater than user-specified value
	 */
	VMEVENT_ATTR_STATE_VALUE_GT	= (1UL << 1),
	/*
	 * Sample value is equal to user-specified value
	 */
	VMEVENT_ATTR_STATE_VALUE_EQ	= (1UL << 2),
	/*
	 * One-shot mode.
	 */
	VMEVENT_ATTR_STATE_ONE_SHOT	= (1UL << 3),

	__VMEVENT_ATTR_STATE_INTERNAL	= (1UL << 30) |
					  (1UL << 31),
};

struct vmevent_attr {
	/*
	 * Value in pages delivered with pointed attribute
	 */
	__u64			value;

	/*
	 * Type of profiled attribute from VMEVENT_ATTR_XXX
	 */
	__u32			type;

        /*
	 * Bitmask of current attribute value (see VMEVENT_ATTR_STATE_XXX)
	*/
	__u32			state;
};

#define VMEVENT_CONFIG_MAX_ATTRS	32

/*
 * Configuration structure to get notifications and attributes values
 */
struct vmevent_config {
	/*
	 * Size of the struct for ABI extensibility.
	 */
	__u32			size;

	/*
	 * Counter of number monitored attributes
	 */
	__u32			counter;

	/*
	 * Sample period in nanoseconds
	 */
	__u64			sample_period_ns;

	/*
	 * Attributes that are monitored and delivered as part of events
	 */
	struct vmevent_attr	attrs[VMEVENT_CONFIG_MAX_ATTRS];
};

struct vmevent_event {
	/*
	 * Counter of attributes in this VM event
	 */
	__u32			counter;

	__u32			padding;

	/*
	 * Attributes for this VM event
	 */
	struct vmevent_attr	attrs[];
};

#endif /* _LINUX_VMEVENT_H */
