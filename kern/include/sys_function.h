

#ifndef _SYSFUNCTION_H_
#define _SYSFUNCTION_H_
int sys_open(userptr_t *filename, int flags, int32_t *retval);
int sys_close(int fd);

#endif