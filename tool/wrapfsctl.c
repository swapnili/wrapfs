#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <getopt.h>
#include <string.h>
#include <linux/types.h>
#include <limits.h>

#include "wrapfs.h"
#define ARRAY_SIZE(arr) (sizeof(arr)/sizeof((arr)[0]))

const char *progname;

static int hide_file(char **args);
static int unhide_file(char **args);
static int block_file(char **args);
static int unblock_file(char **args);
static int list_all(char **args);
static int help(char **args);

struct cmd_opts {
	const char *cmd;
	int (*func) (char **args);
	const char *usage;
} cmds[] = {
	{"hide",	hide_file,	"hide     <file|directory>"},
	{"unhide",	unhide_file,	"unhide   <file|directory>"},
	{"block",	block_file,	"block    <file|directory>"},
	{"unblock",	unblock_file,	"unblock  <file|directory>"},
	{"list",	list_all,	"list"},
	{"help",	help,		"help"},
};

void usage()
{
	int i;

	printf("  %s [options]\n", progname);
	for (i=0; i<ARRAY_SIZE(cmds); i++)
		printf("\t\t%s\n", cmds[i].usage);
}

static int get_inode_number(const char *path, unsigned long *ino)
{
	struct stat stbuf;
	int err;

	err = stat(path, &stbuf);
	if (err) {
		printf("stat failed on %s: %s\n", path, strerror(errno));
		return err;
	}
	*ino = stbuf.st_ino;
	return err;
}

static int do_ioctl(const char *dev, long cmd,
		    struct wrapfs_misc_ioctl *wr_ioctl)
{
	int fd, err;

	fd = open(dev, O_RDWR);
	if (fd < 0) {
		printf("open failed: %s\n", strerror(errno));
		return fd;
	}

	err = ioctl(fd, cmd, wr_ioctl);
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

static int hide_file(char **args)
{
	struct wrapfs_misc_ioctl wr_ioctl = {0};
	int err, cmd;
	char *dev;

	strcpy(wr_ioctl.path, args[0]);
	trim(wr_ioctl.path);

	err = get_inode_number(wr_ioctl.path, &wr_ioctl.ino);
	if (err < 0)
		return err;

	cmd = WRAPFS_IOC_HIDE;
	dev = WRAPFS_CDEV;

	return do_ioctl(dev, cmd, &wr_ioctl);
}

static int unhide_file(char **args)
{
	struct wrapfs_misc_ioctl wr_ioctl = {0};
	int err, cmd;
	char *dev;

	strcpy(wr_ioctl.path, args[0]);
	trim(wr_ioctl.path);

	err = get_inode_number(wr_ioctl.path, &wr_ioctl.ino);
	if (err < 0)
		return err;

	cmd = WRAPFS_IOC_UNHIDE;
	dev = WRAPFS_CDEV;

	return do_ioctl(dev, cmd, &wr_ioctl);
}

static int block_file(char **args)
{
	struct wrapfs_misc_ioctl wr_ioctl = {0};
	int err, cmd;
	char *dev;

	strcpy(wr_ioctl.path, args[0]);
	trim(wr_ioctl.path);

	err = get_inode_number(wr_ioctl.path, &wr_ioctl.ino);
	if (err < 0)
		return err;

	cmd = WRAPFS_IOC_BLOCK;
	dev = wr_ioctl.path;

	return do_ioctl(dev, cmd, &wr_ioctl);
}

static int unblock_file(char **args)
{
	struct wrapfs_misc_ioctl wr_ioctl = {0};
	char *endp;
	int err, cmd;
	char *dev;

	strcpy(wr_ioctl.path, args[0]);
	trim(wr_ioctl.path);
	wr_ioctl.ino = strtoul(args[1], &endp, 10);

	printf("unblock %s %s\n", args[0], args[1]);
	cmd = WRAPFS_IOC_UNBLOCK;
	dev = WRAPFS_CDEV;

	return do_ioctl(dev, cmd, &wr_ioctl);
}

static int list_all(char **args)
{
	struct wrapfs_misc_ioctl wr_ioctl = {0};
	int err, cmd;
	char *dev;

	cmd = WRAPFS_IOC_HIDE_LIST;
	dev = WRAPFS_CDEV;
	return 0;
}

static int help(char **args)
{
	usage();
	return 0;
}

int main(int argc, char **argv)
{
	int min_args = 2, i;

	progname = argv[0];
	if (argc < 2) {
		printf("Invalid argurments\n");
		usage();
		return -EINVAL;
	}

	for (i=0; i<ARRAY_SIZE(cmds); i++) {
		if (!strcmp(cmds[i].cmd, argv[1]))
			return cmds[i].func(&argv[2]);
	}

	printf("unknown option\n");
	usage();
	return -EINVAL;
}
