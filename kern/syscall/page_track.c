#include <page_track.h>
#include <lib.h>
#include <vm.h>
#include <mainbus.h>
#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <synch.h>
#include <vfs.h>
#include <pid.h>
#include <proc.h>
#include <current.h>

struct page_track* page_track_create(void) 
{
	size_t ram = mainbus_ramsize();
	int entry = ram / PAGE_SIZE;
	struct page_track *ret = kmalloc(sizeof(struct page_track));
    ret->memory = kmalloc(entry * sizeof(uint8_t));
    ret->page_lock = lock_create("page_lock");
    ret->entry = entry;
	for (int i = 0; i < entry; i++) {
		ret->memory[i] = (uint8_t)0;
	}
	return ret;
}