#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <getopt.h>
#include <string.h>
#include <sys/types.h>

#include "wrapfs.h"

long cmd;

void usage(char **argv)
{
	printf("Usage: %s [options]\n", argv[0]);
	printf("\toptions:\n");
	printf("\t\t-h|--hide\t[file|directory]\n");
	printf("\t\t-u|--unhide\t[file|directory]\n");
	printf("\t\t-l|--list\t\n");
}

static int do_ioctl(const char *fname)
{
	struct wrapfs_misc_ioctl wr_ioctl = {0};
	struct stat stbuf;
	int fd, err;

	err = stat(fname, &stbuf);
	if (err) {
		printf("stat failed on %s: %s\n", fname, strerror(-err));
		return err;
	}
	strcpy(wr_ioctl.path, fname);
	wr_ioctl.ino = stbuf.st_ino;
	wr_ioctl.sz = 1;

	fd = open(WRAPFS_CDEV, O_RDWR);
	if (fd < 0) {
		printf("open failed: %s\n", strerror(errno));
		return fd;
	}

	err = ioctl(fd, cmd, &wr_ioctl);
	if (err)
		printf("ioctl failed: %s\n", strerror(errno));

	close(fd);
	return err;
}

/* trim leading '/' */
void trim(char *fname)
{
	int sz = strlen(fname);

	if (fname[sz - 1] == '/')
		fname[sz - 1] = '\0';
}

int main(int argc, char **argv)
{
	char *fname;
	int c, err;

	static const struct option longopts[] = {
		{ "hide",	required_argument,	0, 'h'},
		{ "unhide",	required_argument,	0, 'u'},
		{ "list",	no_argument,		0, 'l'},
	};

	if (argc < 2) {
		usage(argv);
		return -EINVAL;
	}

	while((c = getopt_long(argc, argv, "h:u:l", longopts, NULL)) != -1) {
		switch (c){
		case 'h':
			fname = strdup(optarg);
			if (!fname) {
				printf("failed to allocate\n");
				return -ENOMEM;
			}
			cmd = WRAPFS_IOC_HIDE;
			break;
		case 'u':
			fname = strdup(optarg);
			if (!fname) {
				printf("failed to allocate\n");
				return -ENOMEM;
			}
			cmd = WRAPFS_IOC_UNHIDE;
			break;
		case 'l':
			cmd = WRAPFS_IOC_HIDE_LIST;
			break;
		default:
			usage(argv);
			return -EINVAL;
		}
	}

	if (cmd == 0) {
		printf("Invalid options used\n");
		usage(argv);
		return -EINVAL;
	}

	trim(fname);
	err = do_ioctl(fname);
	free(fname);
	return err;
}
