#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <assert.h>
#include <malloc.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#include "linux_dbg_port_protocol.h"
#include "zutexercise.h"
#include "xaxInvokeLinux.h"
#include "xax_network_utils.h"
#include "xpbtable.h"

#define TRAP_ABSORB 1
#if TRAP_ABSORB
#include <linux/unistd.h>
#include "cheesy_snprintf.h"
#include "gsfree_syscall.h"
#include "LiteLib.h"
#endif

// liang: seccomp
#include "seccomp.h"

// liang: checkpoint
#include "checkpoint.h"

void *load_all(const char *path)
{
	int fd = open(path, O_RDONLY);
	assert(fd>=0);
	struct stat statbuf;
	int rc = fstat(fd, &statbuf);
	assert(rc==0);
	uint8_t *buffer = (uint8_t*) malloc(statbuf.st_size);
	off_t done = 0;
	size_t togo = statbuf.st_size;
	while (togo>0)
	{
		int bytes_read = read(fd, buffer+done, togo);
		assert(bytes_read>0);
		togo -= bytes_read;
		done += bytes_read;
	}
	assert(togo==0);
	assert(done==statbuf.st_size);
	return buffer;
}

void display_debugger_help(void *addr);

const char *loader_path;

void jump_from_pal_to_app() // breakpoint target
{
}

#if TRAP_ABSORB
void trap_absorb(int arg)
{
	char buf[1024];
	cheesy_snprintf(buf, sizeof(buf), "caught trap arg %d\n", arg);
	 _gsfree_syscall(__NR_write, 2, buf, lite_strlen(buf));
}

void setup_sigtrap()
{
	// we invoke asm("int $3") to tell the debugger when a new thread
	// has arrive. If that occurs with no debugger present, it manifests
	// as a SIGTRAP and kills the process. Absorb those.
	// Wow, a serious what-the-: signal(SIGTRAP, SIG_IGN) doesn't actually,
	// you know, IGNORE the signal. We have to set up a handler and
	// have that do something. Yaaargh.
	void *rc;
	rc = (void*) signal(SIGTRAP, trap_absorb);
//	rc = (void*) signal(SIGTRAP, SIG_IGN);
		// you'd think this would work, but it don't.
	(void) rc;
//	fprintf(stderr, "signal returned %p; err %p\n", rc, SIG_ERR);
}
#else
void setup_sigtrap() {}
#endif

static scmp_filter_ctx ctx;


// liang: seccomp
void setup_seccomp()
{
	const char *allow_list[] = {
"clone",
"close",
"futex",
"getpid",
"gettid",
"gettimeofday",
"munmap",
"nanosleep",
"open",
"read",
"rt_sigreturn",
"set_thread_area",
"time",
"times",
"write",

// These are pseudo ones on x86
"connect",
"bind",
"shmat",
"shmctl",
"shmget",
"socket",

"ipc",
"mmap2",
"mmap",
"socketcall",
"sigreturn",
	};
	// setup_sigtrap();
	int rc; unsigned int i;
	int syscall = __NR_SCMP_ERROR;
	char *syscall_name;

	// ctx = seccomp_init(SCMP_ACT_ERRNO(-1));
	// ctx = seccomp_init(SCMP_ACT_ALLOW);
	ctx = seccomp_init(SCMP_ACT_KILL);
	// ctx = seccomp_init(SCMP_ACT_TRAP);
	// ctx = seccomp_init(SCMP_ACT_TRACE(6));
	fprintf(stderr, "seccomp_init %d\n", (int)ctx);
	// rc = seccomp_rule_add_exact(ctx, SCMP_ACT_ERRNO(1), SCMP_SYS(open), 0);
	// fprintf(stderr, "seccomp_rule_add_exact,  %d\n", rc);
	
	for (i = 0; i < sizeof(allow_list)/sizeof(void *); i ++){
		syscall = seccomp_syscall_resolve_name(allow_list[i]);
		syscall_name = (char *)allow_list[i];
		rc = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, syscall, 0);
		fprintf(stderr, "rule %d: seccomp_rule_add(%s, %d), rc=%d\n", i, syscall_name, syscall, rc);
		if (rc != 0) exit(1);
	}
	syscall = (int)&allow_list;

	// for (i = 0; i < 400; i++){
	// 	// check syscall number
	// 	if((syscall_name = seccomp_syscall_resolve_num_arch(NULL, i))){
	// 		syscall = i;
	// 		rc = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, syscall, 0);
	// 		fprintf(stderr, "rule %d: seccomp_rule_add(%s, %d), rc=%d\n", i, syscall_name, syscall, rc);
	// 	}
	// }

	rc = seccomp_load(ctx);
	fprintf(stderr, "seccomp_load %d\n", rc);
}


typedef struct {
	uint32_t caller_gs_base;
} XILThreadContext;

void xtc_init(XILThreadContext *xtc);

uint32_t get_gs(void);

int kernel_set_thread_area(struct user_desc *ud);

#define PROTECT_GS_START() \
	{ XILThreadContext __xtc; \
	xtc_init(&__xtc); \
	xil_x86_set_segments_internal(0, 0);
	// Note use of unbalanced {'s to enforce bracketed use in program
	// text. It'll make for horrible compiler errors, but at least
	// they'll be errors.
	// NB also the assumption that the caller puts his gs base
	// at %gs:(0). libc does. Good luck! Well, this won't work for Windows DPI
	// apps. Rats. This isn't a generalizable solution. Really
	// need a %gs-free library.
#define PROTECT_GS_END() \
	xil_x86_set_segments_internal(0, __xtc.caller_gs_base); }


void xil_x86_set_segments_internal(uint32_t fs, uint32_t gs);

int main(int argc, char **argv)
{
	// parse arguments from command line.
	bool wait_for_debugger = false;
	bool raw_binary = false;
	bool is_resume = false;

	argc--; argv++;
	while (argc>0)
	{
		if (strcmp(argv[0], "--raw-binary")==0)
		{
			raw_binary = true;
			argc--; argv++;
		}
		else if (strcmp(argv[0], "--wait-for-debugger")==0)
		{
			wait_for_debugger = true;
			argc--; argv++;
		}
		else if (argv[0][0]=='-')
		{
			fprintf(stderr, "Unknown option %s\n", argv[0]);
			exit(-1);
		}
		else if (strcmp(argv[0], "--resume")==0)
		{
			is_resume = true;
		}
		else
		{
			loader_path = argv[0];
			argc--; argv++;
		}
	}

	printf("PID = %d\n", getpid());
	lz_setup_sig_checkpoint();

	if (is_resume)
		lz_checkpoint_resume();

	setup_sigtrap();

	pal_state_init();

	g_shm_unlink = true;	// normal case
//	g_shm_unlink = false;	// when server is root
	
	while (wait_for_debugger) { sleep(1); }

	void* binary_pointer;
	ZPubKey* endorsing_key = NULL;
	if (raw_binary)
	{
		binary_pointer = load_all(loader_path);
	}
	else
	{
		SignedBinary *signed_binary = (SignedBinary*) load_all(loader_path);

		ZCert *zcert = new ZCert(
			(uint8_t*) &signed_binary[1],
			Z_NTOHG(signed_binary->cert_len));
		endorsing_key = zcert->getEndorsingKey();
		binary_pointer = ((uint8_t*)&signed_binary[1])
			+ Z_NTOHG(signed_binary->cert_len);
	}

	ZoogDispatchTable_v1 *dispatch_table = xil_get_dispatch_table();

	_xil_connect(endorsing_key);
	_xil_start_receive_net_buffer_alarm_thread();

	display_debugger_help(binary_pointer);

	// allocate a little teensy stack with an inaccessible page below it.
	uint8_t *tiny_stack = (uint8_t*) mmap(NULL, 0x3000, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	assert(tiny_stack != MAP_FAILED);
	mprotect(tiny_stack, 0x1000, 0);
	mprotect(tiny_stack+0x2000, 0x1000, 0);
	void *actual_tiny_stack = tiny_stack+0x1000+0x0400;
		// Stack needs to be big enough that the call back into
		// xil_allocate_memory, which uses the same stack, will succeed.

	//liang: sandbox
	// setup_seccomp();
	jump_from_pal_to_app();
	asm(
		"movl	%0,%%esp;"
		"movl	%1,%%ebx;"
		"nop;"
		"movl	%2,%%eax;"
		"jmp	*%%eax;"
		:	// out
		:	"r"(actual_tiny_stack), "r"(dispatch_table) // in
		, "r"(binary_pointer)
		);
	return -1;
}

void display_debugger_help(void *addr)
{
	FILE *fp = fopen("debug_mmap", "a");
	fprintf(fp, "L %s 0x%08x\n", loader_path, (uint32_t) addr);
	fclose(fp);
}
