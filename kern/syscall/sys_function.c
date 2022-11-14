/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009, 2014
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * File-related system call implementations.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/seek.h>
#include <kern/stat.h>
#include <lib.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <copyinout.h>
#include <vfs.h>
#include <vnode.h>
#include <openfile.h>
#include <filetable.h>
#include <syscall.h>
#include <addrspace.h>
#include <mips/trapframe.h>
#include <pid.h>
#include <kern/wait.h>
#include <synch.h>

/*
 * open() - get the path with copyinstr, then use openfile_open and
 * filetable_place to do the real work.
 * 
 */

void childthread(void *newtf, unsigned long data2) ;
int
sys_open(const_userptr_t upath, int flags, mode_t mode, int *retval)
{
	const int allflags =
		O_ACCMODE | O_CREAT | O_EXCL | O_TRUNC | O_APPEND | O_NOCTTY;

	char *kpath;
	struct openfile *file;
	int result;

	if ((flags & allflags) != flags) {
		/* unknown flags were set */
		return EINVAL;
	}

	kpath = kmalloc(PATH_MAX);
	if (kpath == NULL) {
		return ENOMEM;
	}

	/* Get the pathname. */
	result = copyinstr(upath, kpath, PATH_MAX, NULL);
	if (result) {
		kfree(kpath);
		return result;
	}

	/*
	 * Open the file. Code lower down (in vfs_open) checks that
	 * flags & O_ACCMODE is a valid value.
	 */
	result = openfile_open(kpath, flags, mode, &file);
	if (result) {
		kfree(kpath);
		return result;
	}
	kfree(kpath);

	/*
	 * Place the file in our process's file table, which gives us
	 * the result file descriptor.
	 */
	result = filetable_place(curproc->p_filetable, file, retval);
	if (result) {
		openfile_decref(file);
		return result;
	}

	return 0;
}

/*
 * Common logic for read and write.
 *
 * Look up the fd, then use VOP_READ or VOP_WRITE.
 */
static
int
sys_readwrite(int fd, userptr_t buf, size_t size, enum uio_rw rw,
	      int badaccmode, ssize_t *retval)
{
	struct openfile *file;
	bool locked;
	off_t pos;
	struct iovec iov;
	struct uio useruio;
	int result;

	/* better be a valid file descriptor */
	result = filetable_get(curproc->p_filetable, fd, &file);
	if (result) {
		return result;
	}

	/* Only lock the seek position if we're really using it. */
	locked = VOP_ISSEEKABLE(file->of_vnode);
	if (locked) {
		lock_acquire(file->of_offsetlock);
		pos = file->of_offset;
	}
	else {
		pos = 0;
	}

	if (file->of_accmode == badaccmode) {
		result = EBADF;
		goto fail;
	}

	/* set up a uio with the buffer, its size, and the current offset */
	uio_uinit(&iov, &useruio, buf, size, pos, rw);

	/* do the read or write */
	result = (rw == UIO_READ) ?
		VOP_READ(file->of_vnode, &useruio) :
		VOP_WRITE(file->of_vnode, &useruio);
	if (result) {
		goto fail;
	}

	if (locked) {
		/* set the offset to the updated offset in the uio */
		file->of_offset = useruio.uio_offset;
		lock_release(file->of_offsetlock);
	}

	filetable_put(curproc->p_filetable, fd, file);

	/*
	 * The amount read (or written) is the original buffer size,
	 * minus how much is left in it.
	 */
	*retval = size - useruio.uio_resid;

	return 0;

fail:
	if (locked) {
		lock_release(file->of_offsetlock);
	}
	filetable_put(curproc->p_filetable, fd, file);
	return result;
}

/*
 * read() - use sys_readwrite
 */
int
sys_read(int fd, userptr_t buf, size_t size, int *retval)
{
	return sys_readwrite(fd, buf, size, UIO_READ, O_WRONLY, retval);
}

/*
 * write() - use sys_readwrite
 */
int
sys_write(int fd, userptr_t buf, size_t size, int *retval)
{
	return sys_readwrite(fd, buf, size, UIO_WRITE, O_RDONLY, retval);
}

/*
 * close() - remove from the file table.
 */
int
sys_close(int fd)
{
	struct filetable *ft;
	struct openfile *file;

	ft = curproc->p_filetable;

	/* check if the file's in range before calling placeat */
	if (!filetable_okfd(ft, fd)) {
		return EBADF;
	}

	/* place null in the filetable and get the file previously there */
	filetable_placeat(ft, NULL, fd, &file);

	if (file == NULL) {
		/* oops, it wasn't open, that's an error */
		return EBADF;
	}

	/* drop the reference */
	openfile_decref(file);
	return 0;
}

/*
 * lseek() - manipulate the seek position.
 */
int
sys_lseek(int fd, off_t offset, int whence, off_t *retval)
{
	struct stat info;
	struct openfile *file;
	int result;

	/* Get the open file. */
	result = filetable_get(curproc->p_filetable, fd, &file);
	if (result) {
		return result;
	}

	/* If it's not a seekable object, forget about it. */
	if (!VOP_ISSEEKABLE(file->of_vnode)) {
		filetable_put(curproc->p_filetable, fd, file);
		return ESPIPE;
	}

	/* Lock the seek position. */
	lock_acquire(file->of_offsetlock);

	/* Compute the new position. */
	switch (whence) {
	    case SEEK_SET:
		*retval = offset;
		break;
	    case SEEK_CUR:
		*retval = file->of_offset + offset;
		break;
	    case SEEK_END:
		result = VOP_STAT(file->of_vnode, &info);
		if (result) {
			lock_release(file->of_offsetlock);
			filetable_put(curproc->p_filetable, fd, file);
			return result;
		}
		*retval = info.st_size + offset;
		break;
	    default:
		lock_release(file->of_offsetlock);
		filetable_put(curproc->p_filetable, fd, file);
		return EINVAL;
	}

	/* If the resulting position is negative (which is invalid) fail. */
	if (*retval < 0) {
		lock_release(file->of_offsetlock);
		filetable_put(curproc->p_filetable, fd, file);
		return EINVAL;
	}

	/* Success -- update the file structure with the new position. */
	file->of_offset = *retval;

	lock_release(file->of_offsetlock);
	filetable_put(curproc->p_filetable, fd, file);

	return 0;
}

/*
 * dup2() - clone a file descriptor.
 */
int
sys_dup2(int oldfd, int newfd, int *retval)
{
	struct filetable *ft;
	struct openfile *oldfdfile, *newfdfile;
	int result;

	ft = curproc->p_filetable;

	if (!filetable_okfd(ft, newfd)) {
		return EBADF;
	}

	/* dup2'ing an fd to itself automatically succeeds (BSD semantics) */
	if (oldfd == newfd) {
		*retval = newfd;
		return 0;
	}

	/* get the file */
	result = filetable_get(ft, oldfd, &oldfdfile);
	if (result) {
		return result;
	}

	/* make another reference and return the fd */
	openfile_incref(oldfdfile);
	filetable_put(ft, oldfd, oldfdfile);

	/* place it */
	filetable_placeat(ft, oldfdfile, newfd, &newfdfile);

	/* if there was a file already there, drop that reference */
	if (newfdfile != NULL) {
		openfile_decref(newfdfile);
	}

	/* return newfd */
	*retval = newfd;
	return 0;
}

/*
 * chdir() - change directory. Send the path off to the vfs layer.
 */
int
sys_chdir(const_userptr_t path)
{
	char *pathbuf;
	int result;

	pathbuf = kmalloc(PATH_MAX);
	if (pathbuf == NULL) {
		return ENOMEM;
	}

	result = copyinstr(path, pathbuf, PATH_MAX, NULL);
	if (result) {
		kfree(pathbuf);
		return result;
	}

	result = vfs_chdir(pathbuf);
	kfree(pathbuf);
	return result;
}

/*
 * __getcwd() - get current directory. Make a uio and get the data
 * from the VFS code.
 */
int
sys___getcwd(userptr_t buf, size_t buflen, int *retval)
{
	struct iovec iov;
	struct uio useruio;
	int result;

	uio_uinit(&iov, &useruio, buf, buflen, 0, UIO_READ);

	result = vfs_getcwd(&useruio);
	if (result) {
		return result;
	}

	*retval = buflen - useruio.uio_resid;
	return 0;
}

/*Return pid number of the current process in argument retval*/
int 
sys_getpid(int *retval) { 
	*retval = curproc->pid_num;
	return 0; 
}

int
sys___fork( struct trapframe *tf, int *retval) {
	struct proc *newproc;
	int err = 0;
	/*
	newproc = proc_create_runprogram(curproc->p_name);
	if (newproc == NULL) {
		return ENOMEM;
	}
*/
	if(curproc->parent_table == NULL) {
		curproc->parent_table = parent_create();
		if(curproc->parent_table == NULL){
			return ENOMEM;
		}
	}


	err = proc_fork(&newproc);
	if (err) {
		return err;
	}

	err = as_copy(curproc->p_addrspace, &newproc->p_addrspace);
	if (err) {
		proc_destroy(newproc);
		return err;
	}

	//copy trapframe
	struct trapframe *newtf = kmalloc(sizeof(struct trapframe));
	if(newtf == NULL){
		proc_destroy(newproc);
		return ENOMEM;
	}
	
	newtf->tf_vaddr = tf->tf_vaddr;/* coprocessor 0 vaddr register */
	newtf->tf_status = tf->tf_status;/* coprocessor 0 status register */
	newtf->tf_cause = tf->tf_cause;/* coprocessor 0 cause register */
	newtf->tf_lo = tf->tf_lo;
	newtf->tf_hi = tf->tf_hi;
	newtf->tf_ra = tf->tf_ra;		/* Saved register 31 */
	newtf->tf_at = tf->tf_at;		/* Saved register 1 (AT) */
	newtf->tf_v0 = tf->tf_v0;		/* Saved register 2 (v0) */
	newtf->tf_v1 = tf->tf_v1;		/* etc. */
	newtf->tf_a0 = tf->tf_a0;
	newtf->tf_a1 = tf->tf_a1;
	newtf->tf_a2 = tf->tf_a2;
	newtf->tf_a3 = tf->tf_a3;
	newtf->tf_t0 = tf->tf_t0;
	newtf->tf_t1 = tf->tf_t1;
	newtf->tf_t2 = tf->tf_t2;
	newtf->tf_t3 = tf->tf_t3;
	newtf->tf_t4 = tf->tf_t4;
	newtf->tf_t5 = tf->tf_t5;
	newtf->tf_t6 = tf->tf_t6;
	newtf->tf_t7 = tf->tf_t7;
	newtf->tf_s0 = tf->tf_s0;
	newtf->tf_s1 = tf->tf_s1;
	newtf->tf_s2 = tf->tf_s2;
	newtf->tf_s3 = tf->tf_s3;
	newtf->tf_s4 = tf->tf_s4;
	newtf->tf_s5 = tf->tf_s5;
	newtf->tf_s6 = tf->tf_s6;
	newtf->tf_s7 = tf->tf_s7;
	newtf->tf_t8 = tf->tf_t8;
	newtf->tf_t9 = tf->tf_t9;
	newtf->tf_k0 = tf->tf_k0;		/* dummy (see exception-mips1.S comments) */
	newtf->tf_k1 = tf->tf_k1;		/* dummy */
	newtf->tf_gp = tf->tf_gp;
	newtf->tf_sp = tf->tf_sp;
	newtf->tf_s8 = tf->tf_s8;
	newtf->tf_epc = tf->tf_epc;

	//copy kernel thread
	err = thread_fork("Child thread", newproc, childthread, (void *)newtf, 0); // need to figure this out
	if (err) {
		proc_destroy(newproc);
		kfree(newtf);
		return err;
	}
	//proc_destroy(newproc);
	*retval = newproc->pid_num;
	//kprintf("fork: --%d-- \n", newproc->pid_num);
	return 0;
}

/* 
 *This function replaces the currently executing program with a newly loaded program image
 */
int
sys_execv(const char *program, char **args)
{
	struct addrspace *as;
	struct addrspace *oldas;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int err = 0;
	char prog_name[PATH_MAX]; //may need to add err for name_max
	size_t name_sz = 0;
	int argc = 0;
	int args_size = 0;

	//copy in program name
	err = copyinstr((const_userptr_t)program, prog_name, NAME_MAX, &name_sz);
	if (err) {
		return err;
	}

	char *buf = kmalloc(ARG_MAX);
	if (buf == NULL) {
		return ENOMEM;
	}

	//Checking validity of args
	err = copyin((const_userptr_t)args, buf, 4);
	if (err) {
		kfree(buf);
		return err;
	}

	//copy in arguments from the old address space
	for (argc = 0; args[argc] != NULL; argc++) {} //Getting argc
	size_t *argv_sz = kmalloc(sizeof(size_t)*argc);
	if (argv_sz == NULL) {
		kfree(buf);
		return ENOMEM;
	}

	//Getting size of each string
	for (int i = 0; i < argc; i++) {
		err = copyinstr((const_userptr_t)args[i], buf, ARG_MAX, &argv_sz[i]);
		if (err) {
			kfree(buf);
			kfree(argv_sz);
			return err;
		}
		args_size = args_size + argv_sz[i];
		if (args_size > ARG_MAX) {
			kfree(buf);
			kfree(argv_sz);
			return E2BIG;
		}
	}
	kfree(buf);

	char **argv = kmalloc(argc * 4);
	if (argv == NULL) {
		return ENOMEM;
	}
	for (int i = 0; i < argc; i++) {
		argv[i] = kmalloc(argv_sz[i]);
		if (argv[i] == NULL) {
			for (int a = 0; a < i; a++) {
				kfree(argv[a]);
			}
			kfree(argv_sz);
			kfree(argv);
			return ENOMEM;
		}
	}

	for (int i = 0; i < argc; i++) {
		err = copyinstr((const_userptr_t)args[i], argv[i], argv_sz[i], NULL);
		if (err) {
			for (int i = 0; i < argc; i++) {
				kfree(argv[i]);
			}
			kfree(argv_sz);
			kfree(argv);
			return err;
		}
	}

	//Get a new address space
	as = as_create();
	if (as == NULL) {
		for (int i = 0; i < argc; i++) {
			kfree(argv[i]);
		}
		kfree(argv_sz);
		kfree(argv);
		return ENOMEM;
	}

	//Switch to the new address space
	oldas = proc_setas(as);
	as_activate();

	//Load a new executable
	/* Open the file. */
	err = vfs_open(prog_name, O_RDONLY, 0, &v);
	if (err) {
		for (int i = 0; i < argc; i++) {
			kfree(argv[i]);
		}
		kfree(argv_sz);
		kfree(argv);
		as_destroy(as);
		proc_setas(oldas);
		return err;
	}

	err = load_elf(v, &entrypoint);
	if (err) {
		vfs_close(v);
		for (int i = 0; i < argc; i++) {
			kfree(argv[i]);
		}
		kfree(argv_sz);
		kfree(argv);
		as_destroy(as);
		proc_setas(oldas);
		return err;
	}
	vfs_close(v);

	//Define a new stack region
	err = as_define_stack(as, &stackptr);
	if (err) {
		for (int i = 0; i < argc; i++) {
			kfree(argv[i]);
		}
		kfree(argv_sz);
		kfree(argv);
		as_destroy(as);
		proc_setas(oldas);
		return err;
	}

	//Copy the arguments to the new address space, properly arranging them.
	vaddr_t *temp = kmalloc(4 * (argc + 1));
	int paddings;
	for (int i = argc - 1; i >= 0; i--) {
		paddings = 4 - argv_sz[i] % 4;
		if (paddings == 4) {
			paddings = 0;
		}
		stackptr = stackptr - argv_sz[i] - paddings;
		err = copyoutstr(argv[i], (userptr_t)stackptr, argv_sz[i], NULL);
		if (err) {
			for (int a = 0; a < argc; a++) {
				kfree(argv[a]);
			}
			kfree(argv_sz);
			kfree(temp);
			kfree(argv);
			as_destroy(as);
			proc_setas(oldas);
			return err;
		}
		temp[i] = stackptr;
	}
	temp[argc] = (vaddr_t)NULL;

	for (int i = 0; i < argc; i++) {
		kfree(argv[i]);
	}
	kfree(argv);
	kfree(argv_sz);

	for (int i = argc; i >= 0; i--) {
		stackptr = stackptr - 4;
		err = copyout((const void*)&temp[i], (userptr_t)stackptr, 4);
		if (err) {
			as_destroy(as);
			proc_setas(oldas);
			return err;
		}
	}
	kfree(temp);

	//Clean up the old address space
	as_destroy(oldas);

	//Warp to user mode
	enter_new_process(argc /*argc*/, (userptr_t)stackptr /*userspace addr of argv*/,
		NULL /*userspace addr of environment*/,
		stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned in execv\n");
	return EINVAL;
}

void
childthread(void *newtf, unsigned long data2) {
	(void) data2;
	enter_forked_process(newtf);
 //do we need to create new trapframe? what if newtf get freed??
}

int
sys_waitpid(pid_t pid, userptr_t status, int options, int *retval) {
	int err = 0;
	int i;
	
	if (options != 0){
		return EINVAL;
	}
	if(curproc->parent_table == NULL) {
		return ESRCH;
	}
	
	if(pid < 0 || pid > PID_MAX || pid < PID_MIN){
		return ECHILD;
	} 

	lock_acquire(curproc->parent_table->parent_lock);
	for (i = 0; i <= __PID_MAX - 1 ; i++) {
		if(curproc->parent_table->childs[i] != NULL) {
			if (curproc->parent_table->childs[i]->pid_num == pid){
				if(curproc->parent_table->childs[i]->exit == 1){
					err = copyout(&curproc->parent_table->childs[i]->exitcode,status, sizeof(int));
					proc_destroy(curproc->parent_table->childs[i]);
					curproc->parent_table->childs[i] = NULL;
					*retval = pid;
					//kprintf("wait pid done: --%d-- \n", pid);
					break;
				}
				else if(curproc->parent_table->childs[i]->exit == 0){
					lock_acquire(curproc->parent_table->childs[i]->waitlock);
					cv_wait(curproc->parent_table->childs[i]->waitcv, curproc->parent_table->childs[i]->waitlock);
					err = copyout(&curproc->parent_table->childs[i]->exitcode,status, sizeof(int));
					lock_release(curproc->parent_table->childs[i]->waitlock);
					proc_destroy(curproc->parent_table->childs[i]);
					curproc->parent_table->childs[i] = NULL;
					*retval = pid;
					//kprintf("wait pid done: --%d-- \n", pid);
					break;
				}
			}
		}
	}
	if(status == NULL){
		err = 0;
	}
	if(i == __PID_MAX){
		lock_release(curproc->parent_table->parent_lock);
		return ECHILD;
	}
	lock_release(curproc->parent_table->parent_lock);
	return err;
 //do we need to create new trapframe? what if newtf get freed??
}

void
sys_exit(int exitcode){
	//lock_acquire(curproc->parent_table->childs[i]->waitlock);
	curproc->exitcode = _MKWAIT_EXIT(exitcode);
	curproc->exit = 1;
	cv_broadcast(curproc->waitcv, curproc->waitlock);
	//lock_release(curproc->parent_table->childs[i]->waitlock);
	//kprintf("exit dapid: %i", exitcode);
	thread_exit();
}
