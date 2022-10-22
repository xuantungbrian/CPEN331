

#ifndef _SYSFUNCTION_H_
#define _SYSFUNCTION_H_
int sys_open(userptr_t *filename, int flags, int32_t *retval);
int sys_close(int fd);
int sys_dup2(int oldfd, int newfd, int32_t *retval);
int sys_chdir(const char *pathname);
#endif