#pragma once

#include <linux/kvm.h>
#include "MemSlot.h"

struct swap_thread {
	struct kvm_regs regs;
	struct kvm_sregs sregs;
	uint32_t guest_entry_point;
	uint32_t stack_top_guest;
	uint32_t gdt_page_guest_addr;
	bool thread_condemned;
};

struct swap_thread_extra { 
	// to save extra info while resuming
	struct swap_thread thread;
	MemSlot *gdt_page;
};

struct swap_vm {
	uint32_t host_alarms_page_guest_addr;
	uint32_t idt_page_guest_addr;
};

struct swap_file_header {
	uint32_t thread_count;
	struct swap_vm vm;	
};