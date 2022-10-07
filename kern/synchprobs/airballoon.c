/*
 * Driver code for airballoon problem
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

#define N_LORD_FLOWERKILLER 8
#define NROPES 16
static volatile int ropes_left = NROPES; 
static volatile int done = 0;

/* Data structures for rope mappings */
struct rope {
	volatile int cut;
	volatile int number;
};

struct rope *stake[NROPES]; 
struct rope *hook[NROPES];


/* Synchronization primitives */
struct lock *lock;
struct cv* cv;

/*
 * Describe your design and any invariants or locking protocols
 * that must be maintained. Explain the exit conditions. How
 * do all threads know when they are done?
 *
 * Design: Rope class just has 2 fields: number to identify the rope and cut to identify if the rope has
 * been cut (cut = 0 means has not been cut and = 1 if has been cut). "stake" and "hook" are arrays of 
 * pointers that points to the same instances of ropes (stake[0] and hook[0] points to the same rope at 
 * the beginning). The index of the array (stake or hook) represents which stake it is 
 * (for example, stake[0] means stake 0).
 *
 * Locking protocols: There are 4 shared variables needed to be locked: "ropes_left", "done", "stake", 
 * and "hook". Every time they are read or write, the code will acquire the lock and release the lock after 
 * the operations is finished. The cv is used to put the main thread to sleep while other threads are
 * executing. Main thread also acquire lock before going to sleep on cv.
 *
 * Exit conditions: dandelion, marigold, flowekiller and balloon thread will exit when they read global 
 * variable "ropes_left" is 0. After that, they will increment variable "done" and signal the main thread. 
 * When "done" reaches N_LORD_FLOWERKILLER+3 (meaning all forked thread has ended), the main thread will 
 * start executing again (free variables and print "Main thread done").
 * 
 */


static
void
dandelion(void *p, unsigned long arg) 
{
	(void)p;
	(void)arg;

	kprintf("Dandelion thread starting\n"); 

	while (1) {
		int hook_num = random() % NROPES; 

		lock_acquire(lock);

		if (ropes_left <= 0) {
			lock_release(lock);
			break;
		}
		if (hook[hook_num]->cut == 1) {
			lock_release(lock);
			continue;
		}

		//cut ropes by accessing hooks
		hook[hook_num]->cut = 1;
		ropes_left--;
		kprintf("Dandelion severed rope %d\n", hook_num);

		lock_release(lock);

		thread_yield();
	}

	kprintf("Dandelion thread done\n");

	//increment done to notify a forked thread is done
	lock_acquire(lock);
	done++;
	cv_signal(cv, lock);
	lock_release(lock);
}

static
void
marigold(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Marigold thread starting\n");

	while (1) {
		int stake_num = random() % NROPES; 

		lock_acquire(lock);

		if (ropes_left <= 0) {
			lock_release(lock);
			break;
		}
		if (stake[stake_num]->cut == 1) {
			lock_release(lock);
			continue;
		}

		//cut ropes by accessing stakes
		stake[stake_num]->cut = 1;
		ropes_left--;
		kprintf("Marigold severed rope %d from stake %d\n", stake[stake_num]->number, stake_num);

		lock_release(lock);

		thread_yield();
	}

	kprintf("Marigold thread done\n");

	//increment done to notify a forked thread is done
	lock_acquire(lock);
	done++;
	cv_signal(cv, lock);
	lock_release(lock);
}

static
void
flowerkiller(void *p, unsigned long arg) 
{
	(void)p;
	(void)arg;
	int temp;

	kprintf("Lord FlowerKiller thread starting\n");

	while (1) {
		int stake_num_1 = random() % NROPES; 
		int stake_num_2 = random() % NROPES;

		lock_acquire(lock);

		if (ropes_left <= 0) {
			lock_release(lock);
			break;
		}
		if (stake[stake_num_1]->cut == 1 || stake[stake_num_2]->cut == 1 || stake_num_1 == stake_num_2) {
			lock_release(lock);
			continue;
		}

		//exchanging ropes
		temp = stake[stake_num_1]->number;
		stake[stake_num_1]->number = stake[stake_num_2]->number;
		stake[stake_num_2]->number = temp;

		kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", stake[stake_num_1]->number, stake_num_2, stake_num_1);
		kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", stake[stake_num_2]->number, stake_num_1, stake_num_2);

		lock_release(lock);

		thread_yield();
	}

	kprintf("Lord FlowerKiller thread done\n");

	//increment done to notify a forked thread is done
	lock_acquire(lock);
	done++;
	cv_signal(cv, lock);
	lock_release(lock);
}

static
void
balloon(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Balloon thread starting\n");

	//busy-waiting until all ropes are cut (ropes_left = 0)
	while (ropes_left > 0) {}

	kprintf("Balloon freed and Prince Dandelion escapes!\n");
	kprintf("Balloon thread done\n");

	//increment done to notify a forked thread is done
	lock_acquire(lock);
	done++;
	cv_signal(cv, lock);
	lock_release(lock);
}

int
airballoon(int nargs, char **args) 
{
	int err = 0, i;
	done = 0;
	ropes_left = NROPES;
	const char *lk_name = "airballoon_lk";
	const char *cv_name = "airballoon_cv";
	(void)nargs;
	(void)args;
	(void)ropes_left;

	lock = lock_create(lk_name); 
	cv = cv_create(cv_name);

	for (int i = 0; i < NROPES; i++) {
		stake[i] = kmalloc(sizeof(struct rope));
		hook[i] = stake[i];
		hook[i]->cut = 0;
		hook[i]->number = i;
	}

	err = thread_fork("Marigold Thread",
		NULL, marigold, NULL, 0);
	if (err)
		goto panic;

	err = thread_fork("Dandelion Thread",
		NULL, dandelion, NULL, 0);
	if (err)
		goto panic;

	for (i = 0; i < N_LORD_FLOWERKILLER; i++) {
		err = thread_fork("Lord FlowerKiller Thread",
			NULL, flowerkiller, NULL, 0);
		if (err)
			goto panic;
	}

	err = thread_fork("Air Balloon",
		NULL, balloon, NULL, 0);
	if (err)
		goto panic;

	lock_acquire(lock);
	//wait on cv until all forked thread are done
	while (done != N_LORD_FLOWERKILLER + 3) {
		cv_wait(cv, lock);
	}

	lock_release(lock);
	goto done;
panic:
	panic("airballoon: thread_fork failed: %s)\n",
		strerror(err));

done:
	lock_destroy(lock);
	cv_destroy(cv);
	for (int i = 0; i < NROPES; i++) {
		kfree(stake[i]);
	}

	kprintf("Main thread done\n");
	return 0;
}
