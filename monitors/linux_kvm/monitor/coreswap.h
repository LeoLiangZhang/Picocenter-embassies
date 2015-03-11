#pragma once

#include <linux/kvm.h>
#include "MemSlot.h"
#include "pal_abi/pal_net.h"

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
	void *guest_app_code_start;
	char dbg_bootblock_path[256]; // unix path length, should big enough
	XIPifconfig ifconfigs[2];
	uint32_t pub_key_size;
	uint8_t pub_key_serialized[0]; // swap_vm.pub_key_size 
};

struct swap_segment
{
	uint32_t vaddr;
	uint32_t size;
	uint32_t offset; // file offset of the data
};

struct swap_file_header {
	uint32_t swap_vm_size;
	uint32_t thread_count;
	uint32_t segment_count;
	struct swap_vm vm[0];
	struct swap_thread threads[0]; // for ease of access 
	struct swap_segment segments[0];
};
