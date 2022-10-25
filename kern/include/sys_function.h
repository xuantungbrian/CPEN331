

#ifndef _SYSFUNCTION_H_
#define _SYSFUNCTION_H_

/*
Filesystem system calls
Although these system calls may seem to be tied to the filesystem, 
in fact, these system calls are really about manipulation of file descriptors, or process-specific filesystem state(https://sites.google.com/view/cpen331fall2022/assignments/assignment-4)
*/
int sys_open(userptr_t *filename, int flags, int32_t *retval);
int sys_close(int fd);
int sys_dup2(int oldfd, int newfd, int32_t *retval);
int sys_chdir(const char *pathname);
int sys_read(int fd, void *buf, size_t buflen, int* retval);
int sys_write(int fd, const void *buf, size_t nbytes, int* retval);

int sys__getcwd(char *buf, size_t buflen, int32_t *retval);
int sys_lseek(int fd, off_t pos, int whence, off_t* retval);
#endif
