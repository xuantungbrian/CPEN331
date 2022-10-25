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
#include <vm.h>


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
		lock_release(curproc->fd->fdlock);
		return err;
	}

	err = vfs_open(buf, flags, 0, &vn); 
	if (err) { 
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
	int i;
	lock_acquire(curproc->fd->fdlock);
	if (fd >= __OPEN_MAX || fd < 0) {
		lock_release(curproc->fd->fdlock);
		return EBADF; 
	}
	if (curproc->fd->fd_entry[fd] == NULL) {
		lock_release(curproc->fd->fdlock);
		return EBADF; 
	}
	vfs_close(curproc->fd->fd_entry[fd]->vnode_ptr);
	for (i=0; i<__OPEN_MAX; i++) {
		if (curproc->fd->fd_entry[fd] == curproc->fd->fd_entry[i]) {
			break;
		}
	}
	if (i == __OPEN_MAX) {
		kfree(curproc->fd->fd_entry[fd]);
	}
	curproc->fd->fd_entry[fd] = NULL;
	lock_release(curproc->fd->fdlock);
	return 0;
}

int
sys_dup2(int oldfd, int newfd, int32_t *retval)
{   

    lock_acquire(curproc->fd->fdlock);
    if(oldfd < 0 || newfd < 0 || oldfd >= __OPEN_MAX || newfd >= __OPEN_MAX){
        lock_release(curproc->fd->fdlock);
        return 30; // return EBADF
    }
	if(curproc->fd->fd_entry[oldfd] == NULL ){
        lock_release(curproc->fd->fdlock);
        return 30; // return EBADF
    }
    if(oldfd == newfd) {
		*retval = newfd;
        lock_release(curproc->fd->fdlock);
        return 0;
    }
    if(curproc->fd->fd_entry[newfd] != NULL){
        sys_close(newfd);
    }
    curproc->fd->fd_entry[newfd] = curproc->fd->fd_entry[oldfd];
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
	//struct addrspace *addr = curproc->p_addrspace;

	if (buf == NULL) {
		return EFAULT;
	}
    else if ((vaddr_t)buf == 0x40000000 || (vaddr_t)(buf+buflen) == 0x40000000 || (vaddr_t)buf >= USERSPACETOP || (vaddr_t)(buf+buflen) >= USERSPACETOP) {
		return EFAULT;
	}

	iov.iov_ubase = (userptr_t)buf;
	iov.iov_len = buflen;           
	u.uio_iov = &iov;
	u.uio_iovcnt = 1;
	u.uio_resid = buflen;
	u.uio_offset = 0;          
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
	lock_acquire(curproc->fd->fdlock);
	if (fd >= __OPEN_MAX || fd < 0) { //Check invalid fd
		lock_release(curproc->fd->fdlock);
		return EBADF;
	}
	else if (curproc->fd->fd_entry[fd] == NULL) { //Check if fd associated with a vnode
		lock_release(curproc->fd->fdlock);
		return EBADF;
	}
	else if (curproc->fd->fd_entry[fd]->flags == O_WRONLY) { //Check the flags of fd
		lock_release(curproc->fd->fdlock);
		return EBADF;
	}
	if (buf == NULL) { //Check if buf argument is NULL
		lock_release(curproc->fd->fdlock);
		return EFAULT;
	}
	else if ((vaddr_t)buf == 0x40000000 || (vaddr_t)(buf+buflen) == 0x40000000 || (vaddr_t)buf >= USERSPACETOP || (vaddr_t)(buf+buflen) >= USERSPACETOP) { //Check if buf argument is invalid
		lock_release(curproc->fd->fdlock);
		return EFAULT;
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

	//Use uio as a buffer and read from vnode to uio
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

	//Write data from uio to buf pointer
	err = uiomove((void*)buf, amount_read, &u);

	if (err) {
		lock_release(curproc->fd->fdlock);
		return err;
	}
	
	curproc->fd->fd_entry[fd]->offset = curproc->fd->fd_entry[fd]->offset + amount_read;
	lock_release(curproc->fd->fdlock);
	*retval = (int)amount_read;
	return err;
}

int
sys_write(int fd, const void *buf, size_t nbytes, int* retval) 
{
	lock_acquire(curproc->fd->fdlock);
	if (fd >= __OPEN_MAX || fd < 0) { //Check invalid fd
		lock_release(curproc->fd->fdlock);
		return EBADF;
	}
	else if (curproc->fd->fd_entry[fd] == NULL) { //Check if fd associated with a vnode
		lock_release(curproc->fd->fdlock);
		return EBADF;
	}
	else if (curproc->fd->fd_entry[fd]->flags == O_RDONLY) { //Check the flags of fd
		lock_release(curproc->fd->fdlock);
		return EBADF;
	}
	if (buf == NULL) { //Check if buf argument is NULL
		lock_release(curproc->fd->fdlock);
		return EFAULT;
	}
	else if ((vaddr_t)buf == 0x40000000 || (vaddr_t)(buf+nbytes) == 0x40000000 || (vaddr_t)buf >= USERSPACETOP || (vaddr_t)(buf+nbytes) >= USERSPACETOP) { //Check if buf argument is invalid
		lock_release(curproc->fd->fdlock);
		return EFAULT;
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

	//Use uio as a buffer and read data from userland into uio
	err = uiomove((void*)buf, nbytes, &u);
	if (err) {
		lock_release(curproc->fd->fdlock);
		return err;
	}

	iov.iov_kbase = wr_buf;
	iov.iov_len = nbytes;
	u.uio_rw = UIO_WRITE;
	u.uio_offset = curproc->fd->fd_entry[fd]->offset;  
	u.uio_resid = nbytes;

	//Write data from uio to vnode
	err = VOP_WRITE(vn, &u);
	if (err) {
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
	lock_acquire(curproc->fd->fdlock);
	int err = 0;
	struct stat status;
	
	if (fd >= __OPEN_MAX || fd < 0) { //Check invalid fd
		lock_release(curproc->fd->fdlock);
		return EBADF;
	}
	else if (curproc->fd->fd_entry[fd] == NULL) { //Check if fd associated with a vnode
		lock_release(curproc->fd->fdlock);
		return EBADF;
	}
	if (whence != 0 && whence != 1 && whence != 2) { //Check for invalid whence
		lock_release(curproc->fd->fdlock);
		return EINVAL;
	}
	if (VOP_ISSEEKABLE(curproc->fd->fd_entry[fd]->vnode_ptr) != true) { //Check if the file is seekable
		lock_release(curproc->fd->fdlock);
		return ESPIPE;
	}

	int saved_offset = curproc->fd->fd_entry[fd]->offset;

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

