#ifndef _PAGE_TRACK_H_
#define _PAGE_TRACK_H_

#include <types.h>
#include <spinlock.h>

struct page_track {
    uint8_t* memory;
    struct spinlock page_lock;
    int entry;
};

struct page_track* page_track_create(void);

#endif