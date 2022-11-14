#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <synch.h>
#include <vfs.h>
#include <pid.h>
#include <proc.h>
#include <current.h>

struct pid_table* pid_create(void) //do I need to assign pid 1?
{
	struct pid_table *temp = kmalloc(sizeof(struct pid_table));
	temp->pid_lock = lock_create("pid_lock");
	temp->pid[0] = 1;
	temp->pid[1] = 1;
	for (int i = 2; i <= __PID_MAX; i++) {
		temp->pid[i] = 0;
	}
	return temp;
}

pid_t pid_assign(void) 
{
	lock_acquire(kproc->pid_table->pid_lock); //is using kproc like this the right thing?
	for (int i = 2; i <= __PID_MAX; i++) {
		if (kproc->pid_table->pid[i] == 0) {
			kproc->pid_table->pid[i] = 1;
			lock_release(kproc->pid_table->pid_lock);
			return (pid_t)i;
		}
	}
	lock_release(kproc->pid_table->pid_lock);
	//return error EMPROC or ENPROC??
	return ENPROC;
}

void pid_destroy(void)
{
	lock_acquire(kproc->pid_table->pid_lock);
	kproc->pid_table->pid[(int)(curproc->pid_num)] = 0;
	lock_release(kproc->pid_table->pid_lock);
}

  /*char b[] = "[kernel]";
	int i;
	for (i = 0; curproc->p_name[i] != 0 && curproc->p_name[i] == b[i]; i++) {}
	if (curproc->p_name[i] != b[i]) {
		curproc->pid_table = NULL;
		curproc->pid_num = pid_assign();
	}
	else {
		curproc->pid_table = pid_create();
	}*/

struct parent_table* parent_create(void) //do I need to assign pid 1?
{
	struct parent_table *temp = kmalloc(sizeof(struct parent_table));
	if(temp == NULL) {
		return NULL;
	}
	temp->parent_lock = lock_create("parent_lock");
	if(temp->parent_lock == NULL) {
		kfree(temp);
		return NULL;
	}
	for (int i = 0; i <= __PID_MAX - 1; i++) {
		temp->childs[i] = NULL;
	}
	return temp;
}