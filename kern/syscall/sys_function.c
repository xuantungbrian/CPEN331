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
#include <kern/fcntl.h>
#include <kern/stat.h>
#include <endian.h>


int
sys_open(userptr_t *filename, int flags, int *retval)
{
	int rw_flags = flags & O_ACCMODE;
	if (rw_flags != O_RDONLY && rw_flags != O_WRONLY && rw_flags != O_RDWR) {
		lock_release(curproc->fd->fdlock);
		return EINVAL;
	}
	struct vnode* vn;
	int i;
	int err = 0;
	char buf[PATH_MAX];
	size_t *actual = NULL;

	lock_acquire(curproc->fd->fdlock);
	err = copyinstr((const_userptr_t)filename, buf, sizeof(buf), actual); 
	if (err) {
		kprintf("Cannot copy filename from userland\n");
		lock_release(curproc->fd->fdlock);
		return err;
	}

	err = vfs_open(buf, flags, 0, &vn); 
	if (err) { 
		kprintf("Cannot open file\n");
		lock_release(curproc->fd->fdlock);
		return err;
	}

	for (i = 0; i < __OPEN_MAX; i++) { 
		if (curproc->fd->fd_entry[i] == NULL) {
			curproc->fd->fd_entry[i] = kmalloc(sizeof(struct fd_state));
			if (curproc->fd->fd_entry[i] == NULL) {
				lock_release(curproc->fd->fdlock);
				return ENFILE; 
			}
			curproc->fd->fd_entry[i]->offset = 0;
			curproc->fd->fd_entry[i]->vnode_ptr = vn;
			curproc->fd->fd_entry[i]->flags = rw_flags;
			break;
		}
	}

	lock_release(curproc->fd->fdlock);
	if (i == __OPEN_MAX) {
		return EMFILE;
	}

	*retval = i;
	return err;
}

int
sys_close(int fd)
{
	lock_acquire(curproc->fd->fdlock);
	if (curproc->fd->fd_entry[fd] == NULL) {
		kprintf("Close unopened file %d\n", fd);
		lock_release(curproc->fd->fdlock);
		return EBADF; 
	}
	vfs_close(curproc->fd->fd_entry[fd]->vnode_ptr);
	//kfree(curproc->fd->fd_entry[fd]);
	curproc->fd->fd_entry[fd] = NULL;
	lock_release(curproc->fd->fdlock);
	return 0;
}

int
sys_dup2(int oldfd, int newfd, int32_t *retval)
{   

    lock_acquire(curproc->fd->fdlock);

    if(curproc->fd->fd_entry[oldfd] == NULL || oldfd < 0 || newfd < 0 || oldfd > __OPEN_MAX || newfd > __OPEN_MAX){
        lock_release(curproc->fd->fdlock);
        return 30; // return EBADF
    }
    if(oldfd == newfd) {
        lock_release(curproc->fd->fdlock);
        return 0;
    }
    if(curproc->fd->fd_entry[newfd] != NULL){
        sys_close(newfd);
        lock_release(curproc->fd->fdlock);
        return 0;
    }
    curproc->fd->fd_entry[newfd] = curproc->fd->fd_entry[oldfd];
	kprintf(" %p to %p\n", curproc->fd->fd_entry[newfd], curproc->fd->fd_entry[oldfd]);
	kprintf(" %p to %p\n", curproc->fd->fd_entry[newfd]->vnode_ptr, curproc->fd->fd_entry[oldfd]->vnode_ptr);
    curproc->fd->fd_entry[newfd]->vnode_ptr->vn_refcount++;

    *retval = newfd;

    lock_release(curproc->fd->fdlock);
    return 0;
    
}

int
sys_chdir(const char *pathname){

    int err = 0;
    char buf[PATH_MAX];
    size_t *actual = NULL;
	//struct opentable* entry;
    err = copyinstr((const_userptr_t)pathname, buf, sizeof(buf), actual );
    if(err){
        return err;
    }

    err = vfs_chdir((char *) pathname);
    if(err){
        return err;
    }
    return 0;

}

int
sys__getcwd( char *buf, size_t buflen, int32_t *retval){
	int err = 0;
	struct iovec iov;
	struct uio u;
	size_t amount_read;

	u.uio_offset = 0;
	iov.iov_ubase = (userptr_t)buf;
	iov.iov_len = buflen;           
	u.uio_iov = &iov;
	u.uio_iovcnt = 1;
	u.uio_resid = buflen;          
	u.uio_segflg = UIO_SYSSPACE;
	u.uio_rw = UIO_READ;
	u.uio_space = NULL;

	err = vfs_getcwd(&u);
	if(err){
		return err;
	}
	else{
		amount_read = buflen - u.uio_resid;
		*retval = (int32_t)amount_read;
		return err;
	}
}

int
sys_read(int fd, void *buf, size_t buflen, int* retval)
{
	if (fd >= __OPEN_MAX) {
		return EBADF;
	}
	else if (curproc->fd->fd_entry[fd] == NULL) {
		return EBADF;
	}
	else if (curproc->fd->fd_entry[fd]->flags == O_WRONLY) {
		return EBADF;
	}

	struct vnode *vn = curproc->fd->fd_entry[fd]->vnode_ptr;
	int err = 0;
	struct iovec iov;
	struct uio u;
	char rd_buf[buflen];
	size_t amount_read;

	iov.iov_kbase = rd_buf;
	iov.iov_len = buflen;           
	u.uio_iov = &iov;
	u.uio_iovcnt = 1;
	u.uio_resid = buflen;          
	u.uio_offset = curproc->fd->fd_entry[fd]->offset;
	u.uio_segflg = UIO_SYSSPACE;
	u.uio_rw = UIO_READ;
	u.uio_space = NULL;

	lock_acquire(curproc->fd->fdlock);
	err = VOP_READ(vn, &u);
	if (err) {
		lock_release(curproc->fd->fdlock);
		return err;
	}

	amount_read = buflen - u.uio_resid;
	u.uio_rw = UIO_WRITE;
	u.uio_offset = curproc->fd->fd_entry[fd]->offset;
	u.uio_resid = amount_read;
	iov.iov_kbase = rd_buf;
	iov.iov_len = buflen;

	err = uiomove((void*)buf, amount_read, &u);
	if (err) {
		kprintf("Cannot uiomove in sys_read");
		lock_release(curproc->fd->fdlock);
		return err;
	}
	
	curproc->fd->fd_entry[fd]->offset = curproc->fd->fd_entry[fd]->offset + amount_read;
	lock_release(curproc->fd->fdlock);
	*retval = (int)amount_read;
	return err;
}

int
sys_write(int fd, const void *buf, size_t nbytes, int* retval) //definitely need to check for err, add lock
{
	if (fd >= __OPEN_MAX) {
		return EBADF;
	}
	else if (curproc->fd->fd_entry[fd] == NULL) {
		return EBADF;
	}
	else if (curproc->fd->fd_entry[fd]->flags == O_RDONLY) {
		return EBADF;
	}
	
	struct vnode *vn = curproc->fd->fd_entry[fd]->vnode_ptr;
	int err = 0;
	struct iovec iov;
	struct uio u;
	char wr_buf[nbytes];

	iov.iov_kbase = wr_buf;
	iov.iov_len = nbytes;           
	u.uio_iov = &iov;
	u.uio_iovcnt = 1;
	u.uio_resid = nbytes;         
	u.uio_offset = curproc->fd->fd_entry[fd]->offset;
	u.uio_segflg = UIO_SYSSPACE;
	u.uio_rw = UIO_READ;
	u.uio_space = NULL;

	lock_acquire(curproc->fd->fdlock);
	err = uiomove((void*)buf, nbytes, &u);
	if (err) {
		kprintf("Cannot uiomove in sys_write\n");
		lock_release(curproc->fd->fdlock);
		return err;
	}

	iov.iov_kbase = wr_buf;
	iov.iov_len = nbytes;
	u.uio_rw = UIO_WRITE;
	u.uio_offset = curproc->fd->fd_entry[fd]->offset; //not sure if this is the offset of the UIO or the vnode 
	u.uio_resid = nbytes;

	err = VOP_WRITE(vn, &u);
	if (err) {
		kprintf("Cannot VOP_WRITE in sys_write\n");
		lock_release(curproc->fd->fdlock);
		return err;
	}

	*retval = (int)(u.uio_offset - curproc->fd->fd_entry[fd]->offset);
	curproc->fd->fd_entry[fd]->offset = u.uio_offset;
	lock_release(curproc->fd->fdlock);

	return err;
}

int
sys_lseek(int fd, off_t pos, int whence, off_t* retval)
{
	int err = 0;
	struct stat status;
	int saved_offset = curproc->fd->fd_entry[fd]->offset;
	if (fd >= __OPEN_MAX) {
		return EBADF;
	}
	else if (curproc->fd->fd_entry[fd] == NULL) {
		return EBADF;
	}
	if (whence != 0 && whence != 1 && whence != 2) {
		return EINVAL;
	}
	if (VOP_ISSEEKABLE(curproc->fd->fd_entry[fd]->vnode_ptr) != true) {
		return ESPIPE;
	}

	lock_acquire(curproc->fd->fdlock);
	switch (whence) {
		case 0: 
			curproc->fd->fd_entry[fd]->offset = pos;
			break;

		case 1:
			curproc->fd->fd_entry[fd]->offset = curproc->fd->fd_entry[fd]->offset + pos;
			break;

		case 2:
			err = VOP_STAT(curproc->fd->fd_entry[fd]->vnode_ptr, &status);
			if (err) {
				lock_release(curproc->fd->fdlock);
				return err;
			}
			curproc->fd->fd_entry[fd]->offset = status.st_size + pos;
			break;
	}
	lock_release(curproc->fd->fdlock);

	if (curproc->fd->fd_entry[fd]->offset >= 0) {
		*retval = curproc->fd->fd_entry[fd]->offset;
	}
	else {
		curproc->fd->fd_entry[fd]->offset = saved_offset;
		*retval = saved_offset;
		return EINVAL;
	}

	return err;
}

