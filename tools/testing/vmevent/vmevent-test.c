#include "../../../include/linux/vmevent.h"

#if defined(__x86_64__)
#include "../../../arch/x86/include/generated/asm/unistd_64.h"
#endif
#if defined(__arm__)
#include "../../../arch/arm/include/asm/unistd.h"
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <poll.h>

static int sys_vmevent_fd(struct vmevent_config *config)
{
	config->size = sizeof(*config);

	return syscall(__NR_vmevent_fd, config);
}

int main(int argc, char *argv[])
{
	struct vmevent_config config;
	struct pollfd pollfd;
	long phys_pages;
	int fd;
	int i;

	phys_pages = sysconf(_SC_PHYS_PAGES);

	printf("Physical pages: %ld\n", phys_pages);

	config = (struct vmevent_config) {
		.sample_period_ns	= 1000000000L,
		.counter		= 6,
		.attrs			= {
			{
				.type	= VMEVENT_ATTR_NR_FREE_PAGES,
				.state	= VMEVENT_ATTR_STATE_VALUE_LT,
				.value	= phys_pages,
			},
			{
				.type	= VMEVENT_ATTR_NR_FREE_PAGES,
				.state	= VMEVENT_ATTR_STATE_VALUE_GT,
				.value	= phys_pages,
			},
			{
				.type	= VMEVENT_ATTR_NR_FREE_PAGES,
				.state	= VMEVENT_ATTR_STATE_VALUE_LT |
					  VMEVENT_ATTR_STATE_VALUE_GT |
					  VMEVENT_ATTR_STATE_EDGE_TRIGGER,
				.value	= phys_pages / 2,
			},
			{
				.type	= VMEVENT_ATTR_NR_AVAIL_PAGES,
			},
			{
				.type	= VMEVENT_ATTR_NR_SWAP_PAGES,
			},
			{
				.type	= 0xffff, /* invalid */
			},
		},
	};

	fd = sys_vmevent_fd(&config);
	if (fd < 0) {
		perror("vmevent_fd failed");
		exit(1);
	}

	for (i = 0; i < 10; i++) {
		char buffer[sizeof(struct vmevent_event) + config.counter * sizeof(struct vmevent_attr)];
		struct vmevent_event *event;
		int n = 0;
		int idx;

		pollfd.fd		= fd;
		pollfd.events		= POLLIN;

		if (poll(&pollfd, 1, -1) < 0) {
			perror("poll failed");
			exit(1);
		}

		memset(&buffer, 0, sizeof(buffer));

		if (read(fd, &buffer, sizeof(buffer)) < 0) {
			perror("read failed");
			exit(1);
		}

		event = (void *) buffer;

		printf("VM event (%u attributes):\n", event->counter);

		for (idx = 0; idx < event->counter; idx++) {
			struct vmevent_attr *attr = &event->attrs[idx];

			switch (attr->type) {
			case VMEVENT_ATTR_NR_AVAIL_PAGES:
				printf("  VMEVENT_ATTR_NR_AVAIL_PAGES: %Lu\n", attr->value);
				break;
			case VMEVENT_ATTR_NR_FREE_PAGES:
				printf("  VMEVENT_ATTR_NR_FREE_PAGES: %Lu\n", attr->value);
				break;
			case VMEVENT_ATTR_NR_SWAP_PAGES:
				printf("  VMEVENT_ATTR_NR_SWAP_PAGES: %Lu\n", attr->value);
				break;
			default:
				printf("  Unknown attribute: %Lu\n", attr->value);
			}
		}
	}
	if (close(fd) < 0) {
		perror("close failed");
		exit(1);
	}

	return 0;
}
