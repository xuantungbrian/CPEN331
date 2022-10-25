#ifndef _FILETABLE_H_
#define _FILETABLE_H_

#include <spinlock.h>
#include <thread.h> /* required for struct threadarray */
#include <synch.h>
#include <proc.h>
#include <limits.h>
#include <types.h>


/*
Simple fd_state struct for open file table.
offset indicates the position of pointer(read/write) in the file 
flags argument indicates how the file was opened, 
vnode_ptr is an abstract representation of a file.
 */
struct fd_state {
	off_t offset; 
	int flags;
	struct vnode* vnode_ptr;
};

/*
Simple fdtable struct for process file table.
Each process would have one fdtable.
fd_entry is an array containing __OPEN_MAX of fd_state.
Simple fdlock for mutual exclusion of fd_entry.
 */
struct fdtable {
	struct fd_state *fd_entry[__OPEN_MAX];
	struct lock *fdlock;
};

/*
fdtable initialization
create fdtable
*/
struct fdtable *fd_create(void);

#endif /* _FILETABLE_H_ */

