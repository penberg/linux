#include "../../../include/linux/vmnotify.h"

#if defined(__x86_64__)
#include "../../../arch/x86/include/asm/unistd.h"
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <poll.h>

static int sys_vmnotify_fd(struct vmnotify_config *config)
{
	config->size = sizeof(*config);

	return syscall(__NR_vmnotify_fd, config);
}

int main(int argc, char *argv[])
{
	struct vmnotify_config config;
	struct pollfd pollfd;
	int i;
	int fd;

	config = (struct vmnotify_config) {
		.type			= VMNOTIFY_TYPE_SAMPLE | VMNOTIFY_TYPE_FREE_THRESHOLD,
		.event_attrs		= VMNOTIFY_EATTR_NR_AVAIL_PAGES
					| VMNOTIFY_EATTR_NR_FREE_PAGES,
					| VMNOTIFY_EATTR_NR_SWAP_PAGES,
		.sample_period_ns	= 1000000000L,
		.free_threshold		= 99,
	};

	fd = sys_vmnotify_fd(&config);
	if (fd < 0) {
		perror("vmnotify_fd failed");
		exit(1);
	}

	for (i = 0; i < 10; i++) {
		char buffer[sizeof(struct vmnotify_event) + 3 * sizeof(uint64_t)];
		struct vmnotify_event *event;
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

		if (event->attrs & VMNOTIFY_EATTR_NR_AVAIL_PAGES)
			printf("  VMNOTIFY_EATTR_NR_AVAIL_PAGES: %Lu\n", event->attr_values[n++]);

		if (event->attrs & VMNOTIFY_EATTR_NR_FREE_PAGES)
			printf("  VMNOTIFY_EATTR_NR_FREE_PAGES : %Lu\n", event->attr_values[n++]);

		if (event->attrs & VMNOTIFY_EATTR_NR_SWAP_PAGES)
			printf("  VMNOTIFY_EATTR_NR_SWAP_PAGES : %Lu\n", event->attr_values[n++]);
	}
	if (close(fd) < 0) {
		perror("close failed");
		exit(1);
	}

	return 0;
}
