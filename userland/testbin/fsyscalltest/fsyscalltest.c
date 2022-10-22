/*
 * fsyscalltest.c
 *
 * Tests file-related system calls open, close, read and write.
 *
 * Should run on emufs. This test allows testing the file-related system calls
 * early on, before much of the functionality implemented. This test does not
 * rely on full process functionality (e.g., fork/exec).
 *
 * Much of the code is borrowed from filetest.c
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <limits.h>


static int openFDs[OPEN_MAX - 3 + 1];

/*
 * This test makes sure that the underlying filetable implementation
 * allows us to open as many files as is allowed by the limit on the system.
 */

static void
test_dup2()
{
	static char writebuf[41] = 
		"Twiddle dee dee, Twiddle dum dum.......\n";
	static char readbuf[81];
	const char *file;
	int fd, dupfd, rv;

	file = "testfile";

	fd = open(file, O_WRONLY|O_CREAT|O_TRUNC, 0664);
	if (fd<0) {
		err(1, "%s: open for write", file);
	}

	//rv = write(fd, writebuf, 40);
	//if (rv<0) {
	//	err(1, "%s: write", file);
	//}
	
	dupfd = fd + 1;
	rv = dup2(fd, dupfd);
	if (rv<0) {
		err(1, "%s: dup2", file);
	}
	else if(rv != dupfd)
	{
		err(1, "dup2() returned %d, expected %d\n", rv, dupfd);
	}


	rv = close(fd);
	if (rv<0) {
		err(1, "%s: close (original fd)", file);
	}

	rv = close(dupfd);
	if (rv<0) {
		err(1, "%s: close (duplicate)", file);
	}

	fd = open(file, O_RDONLY);
	if (fd<0) {
		err(1, "%s: open for read", file);
	}



	rv = close(fd);
	if (rv<0) {
		err(1, "%s: close (3d time)", file);
	}

	/* ensure null termination */
	readbuf[80] = 0;

	/* Compare the second half */
	if (strcmp(&readbuf[40], writebuf))
	{
		errx(1, "Buffer data mismatch!");
	}

	/* Put a null terminator after the expected
	 * end of the first string and compare 
	 */
	readbuf[40] = 0;
	if (strcmp(readbuf, writebuf)) 
	{
		errx(1, "Buffer data mismatch!");
	}
}

static void
test_openfile_limits()
{
	const char *file;
	int fd, rv, i;

	file = "testfile1";

	/* We should be allowed to open this file OPEN_MAX - 3 times,
	 * because the first 3 file descriptors are occupied by stdin,
	 * stdout and stderr.
	 */
	for (i = 0; i < (OPEN_MAX - 3); i++)
	{
		fd = open(file, O_RDWR | O_CREAT | O_TRUNC, 0664);
		if (fd < 0)
			err(1, "%s: open for %dth time", file, (i + 1));

		if ((fd == 0) || (fd == 1) || (fd == 2))
			err(1, "open for %s returned a reserved file descriptor",
				file);

		/* We do not assume that the underlying system will return
		 * file descriptors as consecutive numbers, so we just remember
		 * all that were returned, so we can close them.
		 */
		openFDs[i] = fd;
	}
	kprintf("still fine 5\n");
	/* This one should fail. */
	fd = open(file, O_RDWR | O_CREAT | O_TRUNC, 0664);
	if (fd > 0)
		err(1, "Opening file for %dth time should fail, as %d "
			"is the maximum allowed number of open files and the "
			"first three are reserved. \n",
			(i + 1), OPEN_MAX);
	kprintf("still fine 6\n");
	/* Let's close one file and open another one, which should succeed. */
	rv = close(openFDs[0]);
	if (rv < 0)
		err(1, "%s: close for the 1st time", file);

	fd = open(file, O_RDWR | O_CREAT | O_TRUNC, 0664);
	if (fd < 0)
		err(1, "%s: re-open after closing", file);

	rv = close(fd);
	if (rv < 0)
		err(1, "%s: close for the 2nd time", file);

	/* Begin closing with index "1", because we already closed the one
	 * at slot "0".
	 */
	for (i = 1; i < OPEN_MAX - 3; i++)
	{
		rv = close(openFDs[i]);
		if (rv < 0)
			err(1, "%s: close file descriptor %d", file, i);
	}
}




/* This test takes no arguments, so we can run it before argument passing
 * is fully implemented.
 */
int
main()
{
	test_openfile_limits();
	test_dup2();
	
	return 0;
}

