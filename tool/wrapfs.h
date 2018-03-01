#ifndef _WRAPFS_H_
#define _WRAPFS_H_

#define MAXNAMELEN	128

#define WRAPFS_IOC_HIDE			_IO('h', 1)
#define WRAPFS_IOC_UNHIDE		_IO('h', 2)
#define WRAPFS_IOC_BLOCK		_IO('h', 3)
#define WRAPFS_IOC_UNBLOCK		_IO('h', 4)
#define WRAPFS_IOC_GET_LIST_SIZE	_IO('h', 5)
#define WRAPFS_IOC_GET_LIST		_IO('h', 6)

/* flags */
#define WRAPFS_HIDE	(1 << 0)
#define WRAPFS_BLOCK	(1 << 1)

struct wrapfs_ioctl {
	unsigned long ino;
	char path[MAXNAMELEN];
	unsigned int flags;
};

struct wrapfs_list_ioctl {
        struct wrapfs_ioctl *list;
        unsigned long size;
};

#define WRAPFS_CDEV     "/dev/wrapfs"

#endif
