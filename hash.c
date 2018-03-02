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

static void free_hnode(struct wrapfs_hnode *wh)
{
	kfree(wh->path);
	kfree(wh);
}

static void remove_hnode(struct wrapfs_hnode *wh)
{
	if (wh) {
		hash_del(&wh->hnode);
		free_hnode(wh);
	}
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
	/* no users */
	if (!wh->flags)
		remove_hnode(wh);
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
	/* no users */
	if (!wh->flags)
		remove_hnode(wh);
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
	if (wh)
		remove_hnode(wh);
	spin_unlock(&sbinfo->hlock);
}

void wrapfs_hide_list_deinit(struct wrapfs_sb_info *sbinfo)
{
	struct wrapfs_hnode *wh;
	struct hlist_node *tmp;
	int i;

	spin_lock(&sbinfo->hlock);
	hash_for_each_safe(sbinfo->hlist, i, tmp, wh, hnode) {
		remove_hnode(wh);
	}
	spin_unlock(&sbinfo->hlock);
}

unsigned long wrapfs_get_list_size(struct wrapfs_sb_info *sbinfo)
{
	struct wrapfs_hnode *wh;
	unsigned long list_sz = 0;
	int i;

	spin_lock(&sbinfo->hlock);
	hash_for_each(sbinfo->hlist, i, wh, hnode)
		list_sz++;
	spin_unlock(&sbinfo->hlock);
	return list_sz;
}

int wrapfs_copy_hlist(struct wrapfs_sb_info *sbinfo,
		      struct wrapfs_ioctl __user *buf, unsigned long size)
{
	struct wrapfs_ioctl *ioctl_buf;
	struct wrapfs_hnode *wh;
	unsigned int bkt, i = 0;
	int ret = 0;

	if (size == 0)
		return -EINVAL;

	ioctl_buf = vmalloc(size * sizeof(struct wrapfs_ioctl));
	if (!ioctl_buf)
		return -ENOMEM;

	hash_for_each(sbinfo->hlist, bkt, wh, hnode) {
		strcpy(ioctl_buf[i].path, wh->path);
		ioctl_buf[i].ino = wh->inode;
		ioctl_buf[i].flags = wh->flags;
		if (++i > size)
			goto out;
	}

out:
	if (copy_to_user(buf, ioctl_buf, size * sizeof(struct wrapfs_ioctl)))
		ret = -EFAULT;

	vfree(ioctl_buf);
	return ret;
}

int wrapfs_get_list(struct wrapfs_sb_info *sbinfo, void __user *buf)
{
	struct wrapfs_list_ioctl list_ioctl;

	if (copy_from_user(&list_ioctl, buf, sizeof(list_ioctl)))
		return -EFAULT;
	return wrapfs_copy_hlist(sbinfo, list_ioctl.list, list_ioctl.size);
}
