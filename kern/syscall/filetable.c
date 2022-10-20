#include <types.h>
#include <spl.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <filetable.h>
#include <limits.h>
#include <synch.h>

struct fdtable *
fd_create()
{
	struct fdtable *fd;
	fd->fdlock = lock_create();
	for(int i = 0; i < 3; i++){
		fd->fd_entry[i] = kmalloc(sizeof(struct opentable));
		if (fd->fd_entry[i] == NULL) {
			return NULL;
		}
		fd->fd_entry[i]->flags = i;
		fd->fd_entry[i]->offset= 0;// confirm offset starts at 0
		if (fd->fd_entry[i]->flags == '\0') {
			return NULL;
		}
		if (fd->fd_entry[i]->offset == '\0') {
			return NULL;
		}
	}
	return fd;
}

