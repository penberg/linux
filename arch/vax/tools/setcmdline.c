/* write a command line into a header block */
/* atp Sept 2001 */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#define COMMAND_LINE_SIZE	256
#define COMMAND_LINE_OFFSET	0x204

static int called_as(char *str1, char *str2) {
	char *tmp;

	tmp = strrchr(str1,'/');
	if (!tmp)
		tmp = str1;
	else
		tmp++;

	return !strncmp(tmp, str2, strlen(str2));
}

int main (int argc, char *argv[]) {
	int kern_fd;
	char buffer[COMMAND_LINE_SIZE];

	if (called_as(argv[0], "showcmdline")) {
		if (argc < 2) {
			printf ("usage: showcmdline kernel_image\n");
			exit(EXIT_FAILURE);
		}
	} else {
		if (argc < 3) {
			printf ("usage: setcmdline kernel_image \"command line\"\n");
			exit(EXIT_FAILURE);
		}
	}

	kern_fd = open(argv[1], O_RDWR);
	if (kern_fd < 0) {
		perror(argv[1]);
		exit(EXIT_FAILURE);
	}

	memset(buffer, 0, COMMAND_LINE_SIZE);

	if (called_as(argv[0], "setcmdline"))  {
		/*
		 * setcmdline
		 */
		if (strlen(argv[2]) >= COMMAND_LINE_SIZE) {
			printf("Warning: Command Line truncated to %d bytes!\n",
					COMMAND_LINE_SIZE - 1);
		}
		strncpy(buffer, argv[2], COMMAND_LINE_SIZE - 1);
		lseek(kern_fd, COMMAND_LINE_OFFSET, SEEK_SET);
		write(kern_fd,buffer,strlen(buffer));
		write(kern_fd,"\0",1);
	} else {
		/*
		 * showcmdline
		 */
		lseek(kern_fd, COMMAND_LINE_OFFSET, SEEK_SET);
		read(kern_fd,buffer, COMMAND_LINE_SIZE - 1);
		printf("\nKernel command line is:\n\t%s\n", buffer);
	}

	close(kern_fd);
	return 0;
}

