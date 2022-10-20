#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <elf.h>
#include <limits.h>
#include <vfs.h>
#include <fs.h>
#include <test.h>
#include <copyinout.h>
#include <synch.h>
#include <filetable.h>
#include <proc.h>
#include <sys_function.h>


int
sys_open(userptr_t *filename, int flags, int32_t *retval)
{
    struct vnode* vn = kmalloc(sizeof(struct vnode));
    int i;
    int err = 0;
    char buf[PATH_MAX];
    size_t *actual = NULL;
	//struct opentable* entry;
    err = copyinstr((const_userptr_t)filename, buf, sizeof(buf), actual ); //must change 4th argument
    if (err) {
        kfree(vn);
		return err;
	}

    err = vfs_open(buf, flags, 0, &vn); // need to add error codes
	if (err) { //if the file is opened, should we return err or add something pointing to the vnode?
        kfree(vn);
		return err;
	}

    lock_acquire(curproc->fd->fdlock);
    for(i = 0; i < __OPEN_MAX ; i++) { //confirm i = 0 or 3
		//entry = proc->fd->fd_entry[i];
        if(curproc->fd->fd_entry[i] == NULL){
            //lock_acquire(fd_lock);
            curproc->fd->fd_entry[i] = kmalloc(sizeof(struct opentable));
            if(curproc->fd->fd_entry[i] == NULL){
                return 29; // return ENFILE
            }
            curproc->fd->fd_entry[i]->offset = 0;// confirm offset starts at 0
            curproc->fd->fd_entry[i]->vnode_ptr = vn;
            curproc->fd->fd_entry[i]->flags = flags;
            //lock_release(fd_lock);
            break;
        }
    }
    lock_release(curproc->fd->fdlock);

    if(i == __OPEN_MAX){
        return 28;// return EMFILE
    }
    *retval = i;
    kprintf("%d\n",i);
    return err;
}


int
sys_close(int fd)
{
    lock_acquire(curproc->fd->fdlock);
    if(curproc->fd->fd_entry[fd] == NULL){
        kprintf("NO!!!!  %d\n",fd);
        return 30; // Return EBDAF - refer to errno.h
    }
    curproc->fd->fd_entry[fd] = NULL;
    lock_release(curproc->fd->fdlock);
    kprintf("closeeeeeeeeeeeeeeeeeeee  %d\n",fd);
    return 0;
}

