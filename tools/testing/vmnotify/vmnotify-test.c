#include "../../../include/linux/vmnotify.h"

#if defined(__x86_64__)
#include "../../../arch/x86/include/asm/unistd.h"
#endif

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
	struct vmnotify_event event;
	struct pollfd pollfd;
	int i;
	int fd;

	config = (struct vmnotify_config) {
		.type			= VMNOTIFY_TYPE_SAMPLE|VMNOTIFY_TYPE_FREE_THRESHOLD,
		.sample_period_ns	= 1000000000L,
		.free_threshold		= 99,
	};

	fd = sys_vmnotify_fd(&config);
	if (fd < 0) {
		perror("vmnotify_fd failed");
		exit(1);
	}

	for (i = 0; i < 10; i++) {
		pollfd.fd		= fd;
		pollfd.events		= POLLIN;

		if (poll(&pollfd, 1, -1) < 0) {
			perror("poll failed");
			exit(1);
		}

		memset(&event, 0, sizeof(event));

		if (read(fd, &event, sizeof(event)) < 0) {
			perror("read failed");
			exit(1);
		}

		printf("VM event:\n");
		printf("\tsize=%lu\n", event.size);
		printf("\tnr_avail_pages=%Lu\n", event.nr_avail_pages);
		printf("\tnr_swap_pages=%Lu\n", event.nr_swap_pages);
		printf("\tnr_free_pages=%Lu\n", event.nr_free_pages);
	}
	if (close(fd) < 0) {
		perror("close failed");
		exit(1);
	}

	return 0;
}
