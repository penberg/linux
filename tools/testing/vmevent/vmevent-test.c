#include "../../../include/linux/vmevent.h"

#if defined(__x86_64__)
#include "../../../arch/x86/include/asm/unistd.h"
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
		.type			= VMEVENT_TYPE_SAMPLE | VMEVENT_TYPE_FREE_THRESHOLD,
		.event_attrs		= VMEVENT_EATTR_NR_AVAIL_PAGES
					| VMEVENT_EATTR_NR_FREE_PAGES
					| VMEVENT_EATTR_NR_SWAP_PAGES,
		.sample_period_ns	= 1000000000L,
		.free_pages_threshold	= phys_pages,
	};

	fd = sys_vmevent_fd(&config);
	if (fd < 0) {
		perror("vmevent_fd failed");
		exit(1);
	}

	for (i = 0; i < 10; i++) {
		char buffer[sizeof(struct vmevent_event) + 3 * sizeof(uint64_t)];
		struct vmevent_event *event;
		int n = 0;

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

		printf("VM event (%Lu bytes):\n", event->size);

		if (event->attrs & VMEVENT_EATTR_NR_AVAIL_PAGES)
			printf("  VMEVENT_EATTR_NR_AVAIL_PAGES: %Lu\n", event->attr_values[n++]);

		if (event->attrs & VMEVENT_EATTR_NR_FREE_PAGES)
			printf("  VMEVENT_EATTR_NR_FREE_PAGES : %Lu\n", event->attr_values[n++]);

		if (event->attrs & VMEVENT_EATTR_NR_SWAP_PAGES)
			printf("  VMEVENT_EATTR_NR_SWAP_PAGES : %Lu\n", event->attr_values[n++]);
	}
	if (close(fd) < 0) {
		perror("close failed");
		exit(1);
	}

	return 0;
}
