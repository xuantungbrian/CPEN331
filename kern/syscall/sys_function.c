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
#include <sys_function.h>

int
sys_open(const char *filename, int flags, int *retval){
    (void) filename;
    (void) flags;
    (void) retval;
    return 0;
}

int
sys_open(const char *filename, int flags, int *retval)
{
    struct vnode* vn = kmalloc(sizeof(struct vnode));
    int i;
    int err = 0;
    char buf[32];
    size_t &actual;
    err = copyinstr((const_userptr_t)filename, buf, sizeof(filename), *actual ); //must change 4th argument
    if (err) {
        kfree(vn);
		return err;
	}

    err = vfs_open(buf, flags, 0, &vn); // need to add error codes
	if (err) { //if the file is opened, should we return err or add something pointing to the vnode?
        kfree(vn);
		return err;
	}


    for(i = 0; i < __OPEN_MAX ; i++) { //confirm i = 0 or 3
        if(file_table[i] == NULL){
            //lock_acquire(fd_lock);
            file_table[i] = kmalloc(sizeof(struct fd_entry));
            if(file_table[i] == NULL){
                return ENFILE;
            }
            file_table[i]->offset = 0;// confirm offset starts at 0
            file_table[i]->vnode_ptr = vn;
            file_table[i]->flags = flags;
            //lock_release(fd_lock);
            break;
        }
    }
    if(i == __OPEN_MAX){
        return EMFILE;
    }
    *retval = i;
    return err;
}
/*
ssize_t
sys_read(int fd, void *buf, size_t buflen, int* retval) //definitely need to check for err, add lock
{
	struct vnode *vn = file_table[fd]->vnode_ptr;
	int err = 0;
	struct iovec iov;
	struct uio u;

	iov.iov_ubase = (userptr_t)vn->vn_data;
	iov.iov_len = memsize;		 // length of the memory space //this line is wrong
	u.uio_iov = &iov;
	u.uio_iovcnt = 1;
	u.uio_resid = filesize;          // amount to read from the file
	u.uio_offset = file_table[fd]->offset;
	u.uio_segflg = UIO_USERSPACE;
	u.uio_rw = UIO_WRITE;
	u.uio_space = NULL;

	err = VOP_READ(vn, &u);
	if (err) {
		return err;
	}
	err = uiomove(buf, buflen, u);
	if (err) {
		return err;
	}
	//*retval = have to return retval
	return err;
}

ssize_t
sys_write(int fd, const void *buf, size_t nbytes, int* retval) //definitely need to check for err, add lock
{
	struct vnode *vn = file_table[fd]->vnode_ptr;
	int err = 0;
	struct iovec iov;
	struct uio u;

	iov.iov_ubase = (userptr_t)vn->vn_data;
	iov.iov_len = memsize;		 // length of the memory space //this line is wrong
	u.uio_iov = &iov;
	u.uio_iovcnt = 1;
	u.uio_resid = filesize;          // amount to read from the file
	u.uio_offset = file_table[fd]->offset;
	u.uio_segflg = UIO_SYSSPACE; //need to check this
	u.uio_rw = UIO_WRITE; //need to check this
	u.uio_space = NULL;
	//line 99 to 106 needs to think again about the logic
	err = uiomove(buf, nbytes, u);
	if (err) {
		return err;
	}
	err = VOP_READ(vn, &u);
	if (err) {
		return err;
	}
	

	//have not return retval, how to find maximum length?
	return err;
}

off_t
sys_lseek(int fd, off_t pos, int whence, int* retval) //add err, lock
{
	int err = 0;
	switch (whence) {
		case SEEK_SET: //add library containing this
			file_table[fd]->offset = pos;
		case SEEK_CUR:
			file_table[fd]->offset = file_table[fd]->offset + pos;
		case SEEK_SET:
			//not sure how to do this?
			//file_table[fd]->offset = end of file + pos
		default:
			//err
	}

	*retval = file_table[fd]->offset;
	return err;
}
*/
int
sys_close(int fd)
{
    if(file_table[fd] == NULL){
        return EBADF;
    }

}