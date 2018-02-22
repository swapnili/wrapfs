#ifndef _WRAPFS_H_
#define _WRAPFS_H_

#define MAXNAMELEN	128

#define WRAPFS_IOC_HIDE         _IO('h', 1)
#define WRAPFS_IOC_UNHIDE       _IO('h', 2)
#define WRAPFS_IOC_BLOCK	_IO('h', 3)
#define WRAPFS_IOC_UNBLOCK	_IO('h', 4)
#define WRAPFS_IOC_HIDE_LIST	_IO('h', 5)

struct wrapfs_misc_ioctl {
	unsigned long sz;
	unsigned long ino;
	char path[MAXNAMELEN];
};

#define WRAPFS_CDEV     "/dev/wrapfs"

#endif
