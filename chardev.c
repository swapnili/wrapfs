/*
 * Copyright (c) 2018 Swapnil Ingle <1985swapnil@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/hashtable.h>
#include <linux/crc32.h>
#include <linux/string.h>
#include <asm/uaccess.h>
#include "wrapfs.h"

#define KEY(x)	(crc32(0, (x), strlen(x)))

static DEFINE_HASHTABLE(hidden_files_hash, 4);

static struct wrapfs_hnode *get_hnode(const char *fname, unsigned long ino)
{
	struct wrapfs_hnode *wh;
	const char *basename = kbasename(fname);
	unsigned key = KEY(basename);

	hash_for_each_possible(hidden_files_hash, wh, hnode, key) {
		if (wh->inode == ino)
			return wh;
	}
	return NULL;
}

int wrapfs_is_hidden(const char *fname, unsigned long inode)
{
	struct wrapfs_hnode *wh;

	wh = get_hnode(fname, inode);
	if (!wh)
		return 0;
	return wh->flags & WRAPFS_HIDE ? 1 : 0;
}

int wrapfs_is_blocked(const char *fname, unsigned long inode)
{
	struct wrapfs_hnode *wh;

	wh = get_hnode(fname, inode);
	if (!wh)
		return 0;
	return wh->flags & WRAPFS_BLOCK ? 1 : 0;
}

static struct wrapfs_hnode *alloc_hnode(const char *fname,
					       unsigned long ino)
{
	struct wrapfs_hnode *wh;

	wh = kzalloc(sizeof(*wh), GFP_KERNEL);
	if (!wh)
		return NULL;

	wh->fname = kstrdup(fname, GFP_KERNEL);
	if (!wh->fname) {
		kfree(wh);
		return NULL;
	}
	wh->inode = ino;
	return wh;
}

int wrapfs_hide_file(const char *fname, unsigned long inode)
{
	struct wrapfs_hnode *wh;
	const char *basename = kbasename(fname);
	unsigned key = KEY(basename);

	wh = get_hnode(fname, inode);
	if (!wh) {
		wh = alloc_hnode(fname, inode);
		if (!wh)
			return -ENOMEM;
		hash_add(hidden_files_hash, &wh->hnode, key);
	}

	wh->flags |= WRAPFS_HIDE;
	printk("hide %s:%lu\n", fname, inode);
	return 0;
}

static void free_hnode(struct wrapfs_hnode *wh)
{
	kfree(wh->fname);
	kfree(wh);
}

int wrapfs_unhide_file(const char *fname, unsigned long ino)
{
	struct wrapfs_hnode *wh;

	wh = get_hnode(fname, ino);
	if (!wh)
		return -ENOENT;

	wh->flags &= ~WRAPFS_HIDE;
	printk("unhide %s:%lu\n", fname, ino);
	return 0;
}

int wrapfs_block_file(struct dentry *dentry, const char *fname,
		      unsigned long ino)
{
	struct wrapfs_hnode *wh;
	struct path lower_path;
	const char *basename = kbasename(fname);
	unsigned key = KEY(basename);

	/* TODO: dont assume dentry priv data exists */
	wrapfs_get_lower_path(dentry, &lower_path);
	wh = get_hnode(fname, ino);
	if (!wh) {
		wh = alloc_hnode(fname, ino);
		if (!wh)
			return -ENOMEM;
		hash_add(hidden_files_hash, &wh->hnode, key);
	}

	wh->flags |= WRAPFS_BLOCK;

	/* unhash dentry */
	d_drop(dentry);
	wrapfs_put_lower_path(dentry, &lower_path);
	printk("block %s:%lu\n", fname, ino);
	return 0;
}

int wrapfs_unblock_file(const char *fname, unsigned long ino)
{
	struct wrapfs_hnode *wh;

	wh = get_hnode(fname, ino);
	if (!wh)
		return -ENOENT;

	wh->flags &= ~WRAPFS_BLOCK;
	printk("unblock %s:%lu\n", fname, ino);
	return 0;
}

void wrapfs_remove_hnode(const char *fname, unsigned long ino)
{
	struct wrapfs_hnode *wh;

	wh = get_hnode(fname, ino);
	if (wh) {
		hash_del(&wh->hnode);
		free_hnode(wh);
	}
}

static void wrapfs_hide_list_deinit(void)
{
	struct wrapfs_hnode *wh;
	struct hlist_node *tmp;
	int i;

	hash_for_each_safe(hidden_files_hash, i, tmp, wh, hnode) {
		hash_del(&wh->hnode);
		free_hnode(wh);
	}
}

static int wrapfs_ioctl_open(struct inode *inode, struct file *file)
{
	return 0;
}

static long wrapfs_misc_ioctl(struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	struct wrapfs_misc_ioctl wr_ioctl;
	void __user *argp = (void __user *)arg;
	int err = 0;

	if (copy_from_user(&wr_ioctl, argp, sizeof(wr_ioctl)))
		return -EFAULT;

	switch (cmd) {
	case WRAPFS_IOC_HIDE:
		err = wrapfs_hide_file(wr_ioctl.path, wr_ioctl.ino);
		break;
	case WRAPFS_IOC_UNHIDE:
		err = wrapfs_unhide_file(wr_ioctl.path, wr_ioctl.ino);
		break;
	case WRAPFS_IOC_BLOCK:
		err = wrapfs_block_file(file_dentry(file), wr_ioctl.path,
					wr_ioctl.ino);
		break;
	case WRAPFS_IOC_UNBLOCK:
		err = wrapfs_unblock_file(wr_ioctl.path, wr_ioctl.ino);
		break;
	case WRAPFS_IOC_HIDE_LIST:
		printk("WRAPFS_IOC_HIDE_LIST\n");
		break;
	default:
		printk("unknown cmd %x\n", cmd);
		return -EINVAL;
	}
	return err;
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
	wrapfs_hide_list_deinit();
}
