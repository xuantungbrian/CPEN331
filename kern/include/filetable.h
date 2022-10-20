#ifndef _FILETABLE_H_
#define _FILETABLE_H_

#include <spinlock.h>
#include <thread.h> /* required for struct threadarray */
#include <synch.h>
#include <proc.h>
#include <limits.h>
struct opentable{
    int offset; //
    int flags;
    struct vnode* vnode_ptr;
};

struct fdtable{
    struct opentable *fd_entry[__OPEN_MAX];
    struct lock *fdlock;
};

struct fdtable *fd_create(void);

#endif /* _FILETABLE_H_ */



