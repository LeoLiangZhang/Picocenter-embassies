#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include "uvmem.h"

#if 1
#define DPRINTF(format, ...) \
	printf("%s:%d "format, __func__, __LINE__, ## __VA_ARGS__)
#else
#define DPRINTF(format, ...)	do { } while (0)
#endif

#define DEV_UVMEM	"/dev/uvmem"
#define UVMEM_NR_PAGES	8

struct pages {
	uint64_t nr;
	uint64_t pgoffs[0];
};

static void server(int uvmem_fd, int shmem_fd, size_t size, size_t page_size)
{
	int nr_pages = size / page_size;

	void* shmem = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
			   shmem_fd, 0);
	if (shmem == MAP_FAILED) {
		err(EXIT_FAILURE, "server: mmap(\"shmem\")");
	}
	close(shmem_fd);

	ssize_t bufsize = nr_pages * sizeof(uint64_t);
	struct pages *page_request = malloc(sizeof(*page_request) + bufsize);
	if (page_request == NULL) {
		err(EXIT_FAILURE, "server: malloc(\"page_request\")");
	}

	struct pages *page_cached = malloc(sizeof(*page_cached) + bufsize);
	if (page_cached == NULL) {
		err(EXIT_FAILURE, "server: malloc(\"page_cached\")");
	}

	int fill = 0;
	fill++;
	memset(shmem, fill, page_size);
	page_cached->pgoffs[0] = 0;

	// DPRINTF("write: 0\n");
	// ssize_t len = sizeof(page_cached->pgoffs[0]);
	// ssize_t written = write(uvmem_fd, page_cached->pgoffs, len);
	// if (written < len) {
	// 	err(EXIT_FAILURE, "server: write");
	// }

	ssize_t len, written;
	int page_served = 0;
	while (page_served < nr_pages) {
		DPRINTF("read\n");
		len = read(uvmem_fd, page_request->pgoffs,
			   sizeof(page_request->pgoffs[0]) * nr_pages);
		if (len < 0) {
			err(EXIT_FAILURE, "server: read");
		}
		page_request->nr = len / sizeof(page_request->pgoffs[0]);


		DPRINTF("request.nr %"PRId64"\n", page_request->nr);
		page_cached->nr = 0;
		int i;
		for (i = 0; i < page_request->nr; ++i) {
			memset(shmem + page_size * page_request->pgoffs[i],
			       fill, page_size);
			fill++;
			// page_cached->pgoffs[page_cached->nr] =
			// 	page_request->pgoffs[i];
			// page_cached->nr++;
			DPRINTF("request[%d] %lx fill: %d\n",
				i, (unsigned long)page_request->pgoffs[i],
				fill - 1);
		}

		DPRINTF("write\n");
		// len = sizeof(page_cached->pgoffs[0]) * page_cached->nr;
		written = write(uvmem_fd, page_request->pgoffs, len);
		if (written < len) {
			err(EXIT_FAILURE, "server: write");
		}
		page_served += page_request->nr;

		// sleep(1);
		 /* Wait for the fault handler completion.
			   * you have to communication with the client.
			   * sleep() is used here for simplicity.
			   */
		// for (i = 0; i < page_request->nr; ++i) {
		// 	madvise(shmem + page_size * page_request->pgoffs[i],
		// 		page_size, MADV_REMOVE);
		// }
	}

#if 0
	/* TODO */
	DPRINTF("UVMEM_MAKE_VMA_ANONYMOUS\n");
	if (ioctl(uvmem_fd, UVMEM_MAKE_VMA_ANONYMOUS)) {
		err(EXIT_FAILURE, "server: UVMEM_MAKE_VMA_ANONYMOUS");
	}
#endif
	munmap(shmem, size);
	close(uvmem_fd);
}

static void client(int uvmem_fd, size_t size, size_t page_size)
{
	DPRINTF("mmap\n");
	void *ram = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE,
			 uvmem_fd, 0);
	if (ram == MAP_FAILED) {
		err(EXIT_FAILURE, "client: mmap");
	}

	DPRINTF("close\n");
	close(uvmem_fd);

	/* do some tasks on the uvmem area */
	int pages[] = {7, 1, 6, 2, 0, 5, 3, 4};
	int val[UVMEM_NR_PAGES];
	int i;
	for (i = 0; i < UVMEM_NR_PAGES; ++i) {
		// if (i == 2 || i == 6)
		// 	sleep(1);
		DPRINTF("access to %d\n", pages[i]);
		fflush(stdout);
		val[i] = *(uint8_t*)(ram + page_size * pages[i]);
		DPRINTF("page:%d val[i=%d]=%d\n", pages[i], i, val[i]);
		*(uint8_t*)(ram + page_size * pages[i]) = 127;
		val[i] = *(uint8_t*)(ram + page_size * pages[i]);
		DPRINTF("written page:%d val[i=%d]=%d\n", pages[i], i, val[i]);
	}

	/* done */
	munmap(ram, size);
}

int main(int argc, char **argv)
{

	int uvmem_fd = open(DEV_UVMEM, O_RDWR);
	if (uvmem_fd < 0) {
		perror("can't open "DEV_UVMEM);
		exit(EXIT_FAILURE);
	}
	printf("uvmem_fd %d\n", uvmem_fd);
	long page_size = sysconf(_SC_PAGESIZE);
	printf("_SC_PAGESIZE = %ld\n", page_size);
	struct uvmem_init uinit = {
		.size = UVMEM_NR_PAGES * page_size,
	};
	printf("UVMEM_INIT = %lu\n", UVMEM_INIT);
	if (ioctl(uvmem_fd, UVMEM_INIT, &uinit) < 0) {
		err(EXIT_FAILURE, "UVMEM_INIT");
	}

	int shmem_fd = uinit.shmem_fd;
	size_t size = uinit.size;

	if (ftruncate(shmem_fd, size) < 0) {
		err(EXIT_FAILURE, "truncate(\"shmem_fd\")");
	}

	printf("uvmem_fd %d shmem_fd %d\n", uvmem_fd, shmem_fd);
	fflush(stdout);

	pid_t child = fork();
	if (child < 0) {
		err(EXIT_FAILURE, "fork");
	}
	if (child == 0) {
		// sleep(1);
		printf("server pid: %d\n", getpid());
		server(uvmem_fd, shmem_fd, size, page_size);
		return 0;
	}

	printf("qemu pid: %d server pid: %d\n", getpid(), child);
	close(shmem_fd);
	// sleep(1);	
	/* wait the daemon is ready
			 * To make it sure, communication with the server
			 * is needed. sleep() is used here for simplicity.
			 */
	client(uvmem_fd, size, page_size);
	return 0;
}
