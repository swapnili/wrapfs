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

static struct wrapfs_hnode *get_hnode(struct wrapfs_sb_info *sbinfo,
				      const char *path, unsigned long ino)
{
	struct wrapfs_hnode *wh;
	const char *basename = kbasename(path);
	unsigned key = KEY(basename);

	hash_for_each_possible(sbinfo->hlist, wh, hnode, key) {
		if (wh->inode == ino)
			return wh;
	}
	return NULL;
}

int wrapfs_is_hidden(struct wrapfs_sb_info *sbinfo, const char *path,
		     unsigned long inode)
{
	struct wrapfs_hnode *wh;
	int hidden = 0;

	spin_lock(&sbinfo->hlock);
	wh = get_hnode(sbinfo, path, inode);
	if (!wh)
		goto out;
	hidden = wh->flags & WRAPFS_HIDE ? 1 : 0;

out:
	spin_unlock(&sbinfo->hlock);
	return hidden;
}

int wrapfs_is_blocked(struct wrapfs_sb_info *sbinfo, const char *path,
		      unsigned long inode)
{
	struct wrapfs_hnode *wh;
	int blocked = 0;

	spin_lock(&sbinfo->hlock);
	wh = get_hnode(sbinfo, path, inode);
	if (!wh)
		goto out;
	blocked = wh->flags & WRAPFS_BLOCK ? 1 : 0;

out:
	spin_unlock(&sbinfo->hlock);
	return blocked;
}

static struct wrapfs_hnode *alloc_hnode(const char *path, unsigned long ino)
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

int wrapfs_hide_file(struct wrapfs_sb_info *sbinfo, const char *path,
		     unsigned long inode)
{
	struct wrapfs_hnode *wh;
	const char *basename = kbasename(path);
	unsigned key = KEY(basename);
	int err = 0;

	spin_lock(&sbinfo->hlock);
	wh = get_hnode(sbinfo, path, inode);
	if (!wh) {
		wh = alloc_hnode(path, inode);
		if (!wh) {
			err = -ENOMEM;
			goto out;
		}
		hash_add(sbinfo->hlist, &wh->hnode, key);
	}
	wh->flags |= WRAPFS_HIDE;
	printk("hide %s:%lu\n", path, inode);

out:
	spin_unlock(&sbinfo->hlock);
	return err;
}

static void free_hnode(struct wrapfs_hnode *wh)
{
	kfree(wh->path);
	kfree(wh);
}

int wrapfs_unhide_file(struct wrapfs_sb_info *sbinfo, const char *path,
		       unsigned long ino)
{
	struct wrapfs_hnode *wh;
	int err = 0;

	spin_lock(&sbinfo->hlock);
	wh = get_hnode(sbinfo, path, ino);
	if (!wh) {
		err = -ENOENT;
		goto out;
	}
	wh->flags &= ~WRAPFS_HIDE;
	printk("unhide %s:%lu\n", path, ino);

out:
	spin_unlock(&sbinfo->hlock);
	return err;
}

int wrapfs_block_file(struct dentry *dentry, const char *path,
		      unsigned long ino)
{
	struct wrapfs_hnode *wh;
	struct wrapfs_sb_info *sbinfo = WRAPFS_SB(dentry->d_sb);
	struct path lower_path;
	const char *basename = kbasename(path);
	unsigned key = KEY(basename);
	int err = 0;

	wrapfs_get_lower_path(dentry, &lower_path);
	spin_lock(&sbinfo->hlock);

	wh = get_hnode(sbinfo, path, ino);
	if (!wh) {
		wh = alloc_hnode(path, ino);
		if (!wh) {
			err = -ENOMEM;
			goto out;
		}
		hash_add(sbinfo->hlist, &wh->hnode, key);
	}

	wh->flags |= WRAPFS_BLOCK;
	/* unhash dentry */
	d_drop(dentry);
	printk("block %s:%lu\n", path, ino);

out:
	spin_unlock(&sbinfo->hlock);
	wrapfs_put_lower_path(dentry, &lower_path);
	return err;
}

int wrapfs_unblock_file(struct wrapfs_sb_info *sbinfo, const char *path,
			unsigned long ino)
{
	struct wrapfs_hnode *wh;
	int err = 0;

	spin_lock(&sbinfo->hlock);
	wh = get_hnode(sbinfo, path, ino);
	if (!wh) {
		err = -ENOENT;
		goto out;
	}
	wh->flags &= ~WRAPFS_BLOCK;
	printk("unblock %s:%lu\n", path, ino);

out:
	spin_unlock(&sbinfo->hlock);
	return 0;
}

void wrapfs_remove_hnode(struct wrapfs_sb_info *sbinfo, const char *path,
			 unsigned long ino)
{
	struct wrapfs_hnode *wh;

	spin_lock(&sbinfo->hlock);
	wh = get_hnode(sbinfo, path, ino);
	if (wh) {
		hash_del(&wh->hnode);
		free_hnode(wh);
	}
	spin_unlock(&sbinfo->hlock);
}

void wrapfs_hide_list_deinit(struct wrapfs_sb_info *sbinfo)
{
	struct wrapfs_hnode *wh;
	struct hlist_node *tmp;
	int i;

	spin_lock(&sbinfo->hlock);
	hash_for_each_safe(sbinfo->hlist, i, tmp, wh, hnode) {
		hash_del(&wh->hnode);
		free_hnode(wh);
	}
	spin_unlock(&sbinfo->hlock);
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
	struct dentry *dentry = file_dentry(file);
	void __user *argp = (void __user *)arg;
	int err = 0;

	if (copy_from_user(&wr_ioctl, argp, sizeof(wr_ioctl)))
		return -EFAULT;

	switch (cmd) {
	case WRAPFS_IOC_UNBLOCK:
		err = wrapfs_unblock_file(WRAPFS_SB(dentry->d_sb), wr_ioctl.path,
					  wr_ioctl.ino);
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
#if 0
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
#endif
	return 0;
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
}
