#ifndef _PID_H_
#define _PID_H_

#include <kern/limits.h>
#include <types.h>

struct pid_table {
	struct lock* pid_lock;
	pid_t pid[__PID_MAX + 1]; //pid[i] = 1 means i has been assigned 
};

struct pid_table* pid_create(void);
pid_t pid_assign(void);
void pid_destroy(void);

#endif /*_PID_H_*/