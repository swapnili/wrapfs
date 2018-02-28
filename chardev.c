/*
 * Copyright (c) 2018 Swapnil Ingle <1985swapnil@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/hashtable.h>
#include <linux/list.h>
#include <linux/vmalloc.h>
#include <linux/crc32.h>
#include <linux/string.h>
#include <asm/uaccess.h>
#include "wrapfs.h"

#define KEY(x)	(crc32(0, (x), strlen(x)))

static DEFINE_HASHTABLE(wrapfs_files_hlist, 4);

static struct wrapfs_hnode *get_hnode(const char *path, unsigned long ino)
{
	struct wrapfs_hnode *wh;
	const char *basename = kbasename(path);
	unsigned key = KEY(basename);

	hash_for_each_possible(wrapfs_files_hlist, wh, hnode, key) {
		if (wh->inode == ino)
			return wh;
	}
	return NULL;
}

int wrapfs_is_hidden(const char *path, unsigned long inode)
{
	struct wrapfs_hnode *wh;

	wh = get_hnode(path, inode);
	if (!wh)
		return 0;
	return wh->flags & WRAPFS_HIDE ? 1 : 0;
}

int wrapfs_is_blocked(const char *path, unsigned long inode)
{
	struct wrapfs_hnode *wh;

	wh = get_hnode(path, inode);
	if (!wh)
		return 0;
	return wh->flags & WRAPFS_BLOCK ? 1 : 0;
}

static struct wrapfs_hnode *alloc_hnode(const char *path,
					       unsigned long ino)
{
	struct wrapfs_hnode *wh;

	wh = kzalloc(sizeof(*wh), GFP_KERNEL);
	if (!wh)
		return NULL;

	wh->path = kstrdup(path, GFP_KERNEL);
	if (!wh->path) {
		kfree(wh);
		return NULL;
	}
	wh->inode = ino;
	return wh;
}

int wrapfs_hide_file(const char *path, unsigned long inode)
{
	struct wrapfs_hnode *wh;
	const char *basename = kbasename(path);
	unsigned key = KEY(basename);

	wh = get_hnode(path, inode);
	if (!wh) {
		wh = alloc_hnode(path, inode);
		if (!wh)
			return -ENOMEM;
		hash_add(wrapfs_files_hlist, &wh->hnode, key);
	}

	wh->flags |= WRAPFS_HIDE;
	printk("hide %s:%lu\n", path, inode);
	return 0;
}

static void free_hnode(struct wrapfs_hnode *wh)
{
	kfree(wh->path);
	kfree(wh);
}

int wrapfs_unhide_file(const char *path, unsigned long ino)
{
	struct wrapfs_hnode *wh;

	wh = get_hnode(path, ino);
	if (!wh)
		return -ENOENT;

	wh->flags &= ~WRAPFS_HIDE;
	printk("unhide %s:%lu\n", path, ino);
	return 0;
}

int wrapfs_block_file(struct dentry *dentry, const char *path,
		      unsigned long ino)
{
	struct wrapfs_hnode *wh;
	struct path lower_path;
	const char *basename = kbasename(path);
	unsigned key = KEY(basename);

	/* TODO: dont assume dentry priv data exists */
	wrapfs_get_lower_path(dentry, &lower_path);
	wh = get_hnode(path, ino);
	if (!wh) {
		wh = alloc_hnode(path, ino);
		if (!wh)
			return -ENOMEM;
		hash_add(wrapfs_files_hlist, &wh->hnode, key);
	}

	wh->flags |= WRAPFS_BLOCK;

	/* unhash dentry */
	d_drop(dentry);
	wrapfs_put_lower_path(dentry, &lower_path);
	printk("block %s:%lu\n", path, ino);
	return 0;
}

int wrapfs_unblock_file(const char *path, unsigned long ino)
{
	struct wrapfs_hnode *wh;

	wh = get_hnode(path, ino);
	if (!wh)
		return -ENOENT;

	wh->flags &= ~WRAPFS_BLOCK;
	printk("unblock %s:%lu\n", path, ino);
	return 0;
}

void wrapfs_remove_hnode(const char *path, unsigned long ino)
{
	struct wrapfs_hnode *wh;

	wh = get_hnode(path, ino);
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

	hash_for_each_safe(wrapfs_files_hlist, i, tmp, wh, hnode) {
		hash_del(&wh->hnode);
		free_hnode(wh);
	}
}

static int wrapfs_ioctl_open(struct inode *inode, struct file *file)
{
	file->private_data = NULL;

	return 0;
}

static long wrapfs_misc_ioctl(struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	struct wrapfs_ioctl wr_ioctl;
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
	case WRAPFS_IOC_UNBLOCK:
		err = wrapfs_unblock_file(wr_ioctl.path, wr_ioctl.ino);
		break;
	default:
		printk("unknown cmd 0x%x\n", cmd);
		return -EINVAL;
	}
	return err;
}

static ssize_t wrapfs_read_hlist(struct file *file, char __user *buf, size_t
				 count, loff_t *ppos)
{
	struct wrapfs_ioctl *ioctl_buf;
	struct wrapfs_hnode *wh;
	unsigned int bucket = *ppos, i = 0;
	unsigned int hashsz = HASH_SIZE(wrapfs_files_hlist);
	int ret;

	ioctl_buf = vmalloc(count);
	if (!ioctl_buf)
		return -ENOMEM;

again:
	if (bucket > hashsz) {
		ret = -ENOENT;
		goto out;
	}

	hlist_for_each_entry(wh, &wrapfs_files_hlist[bucket], hnode) {
		strcpy(ioctl_buf[i].path, wh->path);
		ioctl_buf[i].ino = wh->inode;
		ioctl_buf[i].flags = wh->flags;
		i++;
	}
	bucket++;
	if (i == 0)
		goto again;
	else
		if (copy_to_user(buf, ioctl_buf,
				 i * sizeof(struct wrapfs_ioctl))) {
			ret = -EFAULT;
			goto out;
		}
	*ppos = bucket;
	ret = i;

out:
	vfree(ioctl_buf);
	return ret;
}

static const struct file_operations wrapfs_ctl_fops = {
	.open = wrapfs_ioctl_open,
	.read = wrapfs_read_hlist,
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
