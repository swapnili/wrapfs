/*
 * Copyright (c) 2018 Swapnil Ingle <1985swapnil@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "wrapfs.h"

static int wrapfs_ioctl_open(struct inode *inode, struct file *file)
{
	return 0;
}

static long wrapfs_misc_ioctl(struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	switch (cmd) {
	case WRAPFS_IOC_HIDE_ALL:
		printk("WRAPFS_IOC_HIDE_ALL\n");
		break;
	case WRAPFS_IOC_UNHIDE_ALL:
		printk("WRAPFS_IOC_UNHIDE_ALL\n");
		break;
	case WRAPFS_IOC_HIDE_LIST:
		printk("WRAPFS_IOC_HIDE_LIST\n");
		break;
	default:
		printk("unknown cmd\n");
		return -EINVAL;
	}

	return 0;
}
static const struct file_operations wrapfs_ctl_fops = {
	.open = wrapfs_ioctl_open,
	.unlocked_ioctl = wrapfs_misc_ioctl,
	.compat_ioctl = wrapfs_misc_ioctl,
	.owner = THIS_MODULE,
};

static struct miscdevice wrapfs_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "wrapfs",
	.fops  = &wrapfs_ctl_fops
};

int wrapfs_ioctl_init(void)
{
	return misc_register(&wrapfs_misc);
}

void wrapfs_ioctl_exit(void)
{
	misc_deregister(&wrapfs_misc);
}
