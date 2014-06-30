#pragma once

#include <linux/kvm.h>

struct swap_file_header {
	uint32_t thread_count;
};


struct swap_thread {
	struct kvm_regs regs;
	struct kvm_sregs sregs;
};

struct swap_vm {
	
};

