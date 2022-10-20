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

struct fdtable *
fd_create()
{
    struct fdtable *fd;
	fd = kmalloc(sizeof(struct fdtable));
    //char buf[5] = "con:";
    //int err;
    fd->fdlock = lock_create("fdlock");
    for (int i = 0; i < 3; i++) {
        fd->fd_entry[i] = kmalloc(sizeof(struct opentable));
        if (fd->fd_entry[i] == NULL) {
            return NULL;
        }
        fd->fd_entry[i]->offset = 0;
    }
    return fd;
}