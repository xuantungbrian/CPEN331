#include <types.h>
#include <spl.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <filetable.h>
#include <limits.h>
#include <synch.h>
#include <kern/fcntl.h>
#include <vfs.h>

/*
 * fd_create creates a file table in the process and initialize the first 3 file descriptor to stdin, stdout and stderr
 * fd_create return the pointer to the file table if success. On error, it will return NULL.
 */
struct fdtable *
	fd_create()
{
	struct fdtable *fd = kmalloc(sizeof(struct fdtable));
	struct vnode* vn_1;
	struct vnode* vn_2;
	struct vnode* vn_3;
	char buf[PATH_MAX] = "con:";
	int err = 0;
	fd->fdlock = lock_create("fdlock");
	if (fd->fdlock == NULL) {
		kprintf("Cannot create lock\n");
		return NULL;
	}
	for (int i = 0; i < 3; i++) {
		fd->fd_entry[i] = kmalloc(sizeof(struct fd_state));
		if (fd->fd_entry[i] == NULL) {
			return NULL;
		}
		fd->fd_entry[i]->offset = 0;
	}

	for (int i = 3; i < __OPEN_MAX; i++) {
		fd->fd_entry[i] = NULL;
	}

	//stdin
	err = vfs_open(buf, O_RDONLY, 0664, &vn_1);
	if (err) {
		kprintf("Cannot initialize standard input\n");
		return NULL;
	}
	
	fd->fd_entry[0]->flags = O_RDONLY;
	fd->fd_entry[0]->vnode_ptr = vn_1;
	
	
	 //stdout
	char buf2[PATH_MAX] = "con:";
	err = vfs_open(buf2, O_WRONLY, 0664, &vn_2);
	if (err) {
		kprintf("Cannot initialize standard output\n");
		return NULL;
	}
	fd->fd_entry[1]->flags = O_WRONLY;
	fd->fd_entry[1]->vnode_ptr = vn_2;

	 //stderr
	char buf3[PATH_MAX] = "con:";
	err = vfs_open(buf3, O_WRONLY, 0664, &vn_3);
	if (err) {
		kprintf("Cannot initialize standard error\n");
		return NULL;
	}
	fd->fd_entry[2]->flags = O_WRONLY;
	fd->fd_entry[2]->vnode_ptr = vn_3;
	
	return fd;
}
