#include "checkpoint.h"

// normal libc functions for resuming
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

// #include <unistd.h>
// #include <sys/types.h>
#include <linux/unistd.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <signal.h>

// liang: gsfree related
#include "xax_util.h"
#include <asm/ldt.h>
#include "gsfree_syscall.h"
#include "gsfree_lib.h"

// liang: ptrace
#include <sys/user.h> 
#include <sys/ptrace.h>


#define MAX_THREAD_COUNT 1024
int lz_tid_list[MAX_THREAD_COUNT];
int lz_thread_count = 0;

void lz_gsfree_readline_init(lz_readline_s *reader, int fd)
{
	reader->fd = fd;
	reader->start = NULL;
	reader->end = NULL;
}

int lz_gsfree_readline(lz_readline_s *reader, char *line)
{
	// assuming line is large enough
	// return number of char in the line, excluding \n.
	// TODO: fix buffer overflow in argument line.
	int rc, count = 0;
	while (true) {
		while (reader->start < reader->end) {
			if (*reader->start == '\n') {
				reader->start++;
				*line = '\0';
				return count;
			}
			*line = *reader->start; line++; count++; reader->start++;
		}
		rc = _gsfree_syscall(__NR_read, reader->fd, reader->data, READLINE_DATA_SIZE);
		if (rc == 0){
			return count;
		} else if (rc > 0) {
			reader->start = reader->data;
			reader->end = reader->start + rc;
		} else {
			return -count; // error;
		}
	}
}

int lz_gsfree_write(int fp, const void *buf, int count)
{
	return _gsfree_syscall(__NR_write, fp, buf, count);
}

unsigned int lz_strtoul(char *start, char **end, int base)
{
	// only support hex, not safe for general purpose
	// would not emit error code
	const char *digits = "0123456789abcdef";
	char *ptr = start;
	int stack[50];
	int i, sp = 0, flag = 1;
	unsigned int result = 0, times = 1;
	while (flag) {
		flag = 0;
		for (i = 0; i < 17; i++) {
			if (*ptr == digits[i]){
				stack[sp++] = i;
				flag = 1;
				break;
			}
		}
		*end = ptr++;
	}
	for (i = sp - 1; i >= 0; i--) {
		result += stack[i] * times;
		times *= base;
	}
	return result;
}

// implemented in xaxInvokeLinux.cpp
int _gsfree_gettid(void); 
int _gsfree_getpid(void);

int _gsfree_ptrace(enum __ptrace_request request, pid_t pid, void *addr, void *data)
{
	return _gsfree_syscall(__NR_ptrace, request, pid, addr, data);
}

void lz_checkpoint_handler(int arg)
{
	// TODO: try setjump/longjmp in checkpoint/resume in DBG.
	int rc, i;
	int fp0, fp1;

	// open/create checkpoint_dump file
	fp0 = _gsfree_syscall(__NR_open, "checkpoint_dump", O_WRONLY|O_CREAT, 0660);
	gsfree_assert(fp0);

	// save threads registers
	int tid, current_tid = _gsfree_gettid();
	struct user_regs_struct regs;
	rc = lz_gsfree_write(fp0, &lz_thread_count, sizeof(lz_thread_count));
	gsfree_assert(rc);
	for (i = 0; i < lz_thread_count; i++) {
		tid = lz_tid_list[i];
		if (tid == current_tid) continue;
		rc = _gsfree_ptrace(PTRACE_ATTACH, tid, NULL, NULL);
		gsfree_assert(rc);
		rc = _gsfree_syscall(__NR_waitpid, -1, NULL, 0); // equals: wait(NULL)
		gsfree_assert(rc);
		rc = _gsfree_ptrace(PTRACE_GETREGS, tid, NULL, &regs);
		gsfree_assert(rc);
		rc = lz_gsfree_write(fp0, &regs, sizeof(regs));
		gsfree_assert(rc);
	}

	// open the process's memory maps
	fp1 = _gsfree_syscall(__NR_open, "/proc/self/maps", O_RDONLY);
	gsfree_assert(fp1);
	
	// parse proc maps file, and dump pages
	int count;
	char buf[4096], *endptr;
	uint32_t start, end;
	lz_page_desc_s page;
	lz_readline_s reader;
	lz_gsfree_readline_init(&reader, fp1);
	while((count = lz_gsfree_readline(&reader, buf)))
	{
		rc = lz_gsfree_write(2, "Saving: ", 8);
		gsfree_assert(rc);
		rc = lz_gsfree_write(2, buf, count);
		gsfree_assert(rc);
		rc = lz_gsfree_write(2, "\n", 1);
		gsfree_assert(rc);
		// ex: 
		// 006f2000-006fb000 rw-p 000f2000 08:07 6032167                            /bin/bash
		start = lz_strtoul(buf, &endptr, 16);
		end = lz_strtoul(endptr + 1, &endptr, 16); // +1 for offset "-"
		page.start = start;
		page.size = end - start;
		page.prot = PROT_NONE;
		if (*(endptr+1) == 'r') page.prot |= PROT_READ;
		if (*(endptr+2) == 'w') page.prot |= PROT_WRITE;
		if (*(endptr+3) == 'x') page.prot |= PROT_EXEC;
		rc = lz_gsfree_write(fp0, &page, sizeof(page));
		gsfree_assert(rc);
		if (page.prot == PROT_NONE) continue; 
		// breaks here if the page is not readable
		// but, why page is set to unreadable?
		rc = lz_gsfree_write(fp0, (char *)page.start, page.size);
		gsfree_assert(rc);
	}
	_gsfree_syscall(__NR_open, fp1);
	_gsfree_syscall(__NR_open, fp0);

	// _gsfree_syscall(__NR_exit, 0);
	exit(0);
}

void lz_fill_pages(lz_page_loader *loader, uint32_t low, uint32_t high)
{
	// Fill saved pages into memory gap between low and high.
	if (low == high) return;

	int rc;
	FILE *fp = loader->fp;

	while(1) {
		if (loader->saved_page.size == 0) {
			loader->page_desc_offset = ftell(fp);
			assert(loader->page_desc_offset);
			rc = fread(&loader->saved_page, sizeof(loader->saved_page), 1, fp);
			if (rc == 0) return; // EOF	
			loader->page_data_offset = loader->page_desc_offset + sizeof(lz_page_desc_s);
		}

		if (loader->saved_page.start >= low && 
			  loader->saved_page.start < high) {
			int length = high - loader->saved_page.start;
			if (length > loader->saved_page.size){
				length = loader->saved_page.size;
			}
			void *addr = mmap((void *)loader->saved_page.start, length, 
				PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1 , 0);
			assert(addr == (void *)loader->saved_page.start);
			rc = fseek(fp, loader->page_data_offset, SEEK_SET);
			assert(rc);
			rc = fread(addr, length, 1, fp);
			assert(rc);
			loader->saved_page.start += length;
			loader->saved_page.size -= length;
			loader->page_data_offset += length;
		} else {
			break;
		}
	}
}

void lz_checkpoint_resume()
{
	int rc;
	FILE *fp0 = fopen("checkpoint_dump", "r");
	assert(fp0);
	int thread_count;
	rc = fread(&thread_count, sizeof(thread_count), 1, fp0);
	assert(rc);

	// TODO: resume threads;
	
	int threads_size = sizeof(struct user_regs_struct) * thread_count;
	rc = fseek(fp0, threads_size, SEEK_CUR);
	assert(rc);
	lz_page_loader loader;
	loader.fp = fp0;
	loader.saved_page.size = 0;

	FILE *fp1 = fopen("/proc/self/maps", O_RDONLY);
	assert(fp1);
	char buf[4096], *endptr;
	uint32_t start, end, last_end;
	lz_page_desc_s page;

	while (fgets(buf, sizeof(buf), fp1)) {
		
		fprintf(stderr, "Parsing: %s", buf);
		start = lz_strtoul(buf, &endptr, 16);
		end = lz_strtoul(endptr + 1, &endptr, 16);
		page.start = start;
		page.size = end - start;
		page.prot = PROT_NONE;
		if (*(endptr+1) == 'r') page.prot |= PROT_READ;
		if (*(endptr+2) == 'w') page.prot |= PROT_WRITE;
		if (*(endptr+3) == 'x') page.prot |= PROT_EXEC;

		// invariant: saved_page.start <= page.start
		if ((page.prot & PROT_WRITE) == 0) {
			last_end = end;
			continue;
		}
		lz_fill_pages(&loader, last_end, end);
	}
	lz_fill_pages(&loader, last_end, ~0);

}

void lz_setup_sig_checkpoint()
{
	void *rc;
	rc = (void*) signal(SIGUSR1, lz_checkpoint_handler);
	(void) rc;
}
