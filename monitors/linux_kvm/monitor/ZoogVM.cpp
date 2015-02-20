#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/kvm.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <stdint.h>

// liang: mmap support
#include <signal.h>

#include "ZoogVM.h"
#include "ZoogVCPU.h"
#include "MemSlot.h"
#include "SyncFactory_Pthreads.h"
#include "corefile.h"
#include "safety_check.h"
#include "VCPUPool.h"
#include "KeyDerivationKey.h"

ZoogVM::ZoogVM(MallocFactory *mf, MmapOverride *mmapOverride, bool wait_for_core)
	: crypto(MonitorCrypto::NO_MONITOR_KEY_PAIR)
{
	this->mf = mf;
	this->sf = new SyncFactory_Pthreads();
	this->mmapOverride = mmapOverride;
	this->wait_for_core = wait_for_core;
	this->mutex = sf->new_mutex(false);
	this->memory_map_mutex = sf->new_mutex(false);
	guest_memory_allocator = new CoalescingAllocator(sf, true);
	net_buffer_table = new NetBufferTable(this, sf);
	zutex_table = new ZutexTable(mf, sf, this);

	linked_list_init(&vcpus, mf);
	vcpu_pool = new VCPUPool(this);
	pub_key = NULL;
	guest_app_code_start = NULL;

#if DBG_SEND_FAILURE
	dbg_send_log_fp = fopen("monitor_send_log", "w");
#endif // DBG_SEND_FAILURE

	_setup(false);

	// Need guest memory allocator available to allocate alarms page
	host_alarms_page = allocate_guest_memory(PAGE_SIZE, "host_alarms_page");
		// we keep a handle to this page to keep client from deallocating
		// it, and causing us to later deref invalid memory.
		// TODO I guess we should do the same for the call page, huh?
	host_alarms = new HostAlarms(zutex_table, host_alarms_page);
	alarm_thread = new AlarmThread(host_alarms);

	TunIDAllocator* tunid = new TunIDAllocator();
	coordinator = new CoordinatorConnection(mf, sf, tunid, host_alarms);

	idt_page = allocate_guest_memory(PAGE_SIZE, "idt_page");
	memset(idt_page->get_host_addr(), 0, idt_page->get_size());
}

ZoogVM::ZoogVM(MallocFactory *mf, MmapOverride *mmapOverride, bool wait_for_core, const char *core_file)
	: crypto(MonitorCrypto::NO_MONITOR_KEY_PAIR)
{
	// liang: swapon
	// resume from checkpoint
	this->mf = mf;
	this->sf = new SyncFactory_Pthreads();
	this->mmapOverride = mmapOverride;
	this->wait_for_core = wait_for_core;
	this->mutex = sf->new_mutex(false);
	this->memory_map_mutex = sf->new_mutex(false);
	guest_memory_allocator = new CoalescingAllocator(sf, true);
	net_buffer_table = new NetBufferTable(this, sf);
	zutex_table = new ZutexTable(mf, sf, this);

	linked_list_init(&vcpus, mf);
	vcpu_pool = new VCPUPool(this);
	pub_key = NULL;
	guest_app_code_start = NULL;

#if DBG_SEND_FAILURE
	dbg_send_log_fp = fopen("monitor_send_log", "w");
#endif // DBG_SEND_FAILURE

	_setup(true);

	struct swap_vm *vm;
	_load_swap(core_file, &vm);

	// Need guest memory allocator available to allocate alarms page
	// liang: resume would do 
	// host_alarms_page = allocate_guest_memory(PAGE_SIZE, "host_alarms_page");
		// we keep a handle to this page to keep client from deallocating
		// it, and causing us to later deref invalid memory.
		// TODO I guess we should do the same for the call page, huh?
	host_alarms = new HostAlarms(zutex_table, host_alarms_page);
	alarm_thread = new AlarmThread(host_alarms);

	TunIDAllocator* tunid = new TunIDAllocator();
	coordinator = new CoordinatorConnection(mf, sf, tunid, host_alarms);

	// liang: _load_swap would do
	// idt_page = allocate_guest_memory(PAGE_SIZE, "idt_page");
	// memset(idt_page->get_host_addr(), 0, idt_page->get_size());

	// liang: reconnect coordinator
	if(this->pub_key){
		// set_pub_key(this->pub_key);
		_resume_coordinator(this->pub_key, vm->ifconfigs);
	}

	delete vm; // make sure recycle it
}

struct idt_table_entry {
	uint16_t offset_hi;
	uint16_t flags;
	uint16_t segment_selector;
	uint16_t offset_lo;
};

void ZoogVM::set_idt_handler(uint32_t target_address)
{
	// Set up IDT (see Figure 6-2, page Vol. 3A 6-15, page 2405/4060)
	struct idt_table_entry *idt_entries =
		(struct idt_table_entry *) idt_page->get_host_addr();
	lite_assert(sizeof(struct idt_table_entry)==8);
	uint32_t i;
	for (i=0; i<idt_page->get_size()/sizeof(struct idt_table_entry); i++)
	{
		idt_entries[i].offset_hi = (target_address >> 16) & 0x0ffff;
		idt_entries[i].flags = 0
			| (1<<15)	// segment present
			| (0<<13)	// DPL==0
			| (1<<11)	// 32-bit gate
			| (6<<8)	// interrupt/trap flags constant
			| (0<<8)	// interrupt, not trap
			;
		idt_entries[i].segment_selector = gdt_index_to_selector(gdt_code_flat);
		idt_entries[i].offset_lo = (target_address >> 0) & 0x0ffff;
	}
}

void ZoogVM::_map_physical_memory(bool resume)
{
	// TODO Just use MAP_FIXED, and don't bother with the munmap. Problem solved.
	/* TODO Ugh. We have a race condition where during our unmap/map to remap
	the guest memory, another thread may mmap some host memory and have
	linux fill it in (top first) right in the middle of client memory
	space. Ack! That causes an assert shortly after (when the mmap into
	the munmap'd region fails), but it's still a race condition that
	could lead to an exploit. The ultimate fix is to fold EVERY mmap,
	host and guest, into the lock, presumably by overriding mmap().
	But as an intermediate expedient, we map the guest region down low,
	where Linux isn't inclined to allocate, until we fill memory.
	This is a stupid stopgap, since an attacker could presumably convince
	us to fill host memory, e.g. through some leak.*/

	void *desired_host_phys_addr = (void*) 0x18000000;
	host_phys_memory = (uint8_t*) mmap(
		desired_host_phys_addr, host_phys_memory_size,
		PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	lite_assert(host_phys_memory != MAP_FAILED);
	lite_assert(host_phys_memory == desired_host_phys_addr);

	if(!resume)
		guest_memory_allocator->create_empty_range(Range(0x10001000, host_phys_memory_size));

	struct kvm_userspace_memory_region umr;
	umr.slot = 0;
	umr.flags = 0;
	umr.guest_phys_addr = 0;
	umr.memory_size = host_phys_memory_size;
	umr.userspace_addr = ((__u64) host_phys_memory) & 0x0ffffffffL;

	int rc = ioctl(vmfd, KVM_SET_USER_MEMORY_REGION, &umr);
	if (rc!=0)
	{
		munmap(host_phys_memory, host_phys_memory_size);
		lite_assert(false);
	}
}

void ZoogVM::_setup_kvm()
{
	int rc;

	kvmfd=open("/dev/kvm", O_RDWR);
	perror("Open kvm");
	lite_assert(kvmfd>=0);

	int api_version = ioctl(kvmfd, KVM_GET_API_VERSION, 0);
	lite_assert(api_version >= 12);

	vmfd = ioctl(kvmfd, KVM_CREATE_VM, 0);
	lite_assert(vmfd>=0);

	// ioctls that strace shows qemu(-kvm) using, that I think we can
	// skip:
	// I don't think we need KVM_SET_TSS_ADDR, because we don't care about
	// 'vm86' mode, which apparently is what needs a TSS.
	// KVM_SET_IDENTITY_MAP_ADDR -- something about BIOS placement; we
	// probably don't care about it.
	// KVM_CREATE_PIT -- something about interval timer emulation. I think
	// we don't need to care.
	// KVM_CREATE_IRQCHIP -- probably don't need it.
	// KVM_SET_GSI_ROUTING -- more funky simulated IRQ lines mumble mumble
	// that we don't care about.
	// KVM_SET_BOOT_CPU_ID -- probably don't care
	// KVM_IRQ_LINE_STATUS

	// cargo cult
	// I don't know what this does, or why it matters for our 32-bit
	// processes, or why it works (since it points at unmapped guest
	// memory), but it's necessary.
	rc = ioctl(vmfd, KVM_SET_TSS_ADDR, 0xfeffd000);
	lite_assert(rc==0);

	//KVM_CAP_USER_MEMORY seems purdy important to qemu-kvm...
	// unknown:
	// KVM_X86_SETUP_MCE -- what's that?
	// KVM_TPR_ACCESS_REPORTING
	// KVM_CHECK_EXTENSION, 0x10 -- haven't inferred which this is.

	// KVM_SET_REGS -- probably important!
	// KVM_SET_FPU
	// KVM_SET_SREGS
	// KVM_SET_MSRS

	rc = ioctl(kvmfd, KVM_CHECK_EXTENSION, KVM_CAP_USER_MEMORY);
	lite_assert(rc>0);

	// We want to map in one big region, then parcel it out to the guest
	// with mprotect.
	rc = ioctl(kvmfd, KVM_CHECK_EXTENSION, KVM_CAP_SYNC_MMU);
	lite_assert(rc>0);

	rc = ioctl(kvmfd, KVM_CHECK_EXTENSION, KVM_CAP_DESTROY_MEMORY_REGION_WORKS);
	lite_assert(rc>0);
}

void ZoogVM::_setup(bool resume)
{
	_setup_kvm();

	_map_physical_memory(resume);

	// For some reason, we need to allocate memory at page zero. I'm
	// not at all sure why; I'd rather not! I guess we might be able
	// to make it inaccessible to the actual pal code by turning on
	// PE (paging).
	// I wonder what the processor is looking for at page 0? I mean,
	// we've set up gdt, ldt, and idt not to point there. Weird.

	// It's something to do with page tables; failing to do this causes
	// KVM_EXIT_INTERNAL_ERROR. But (a) we have paging turned off, right?
	// (maybe linux' emulation doesn't know that?) and
	// (b) wait, this page isn't even at zero anymore, is it?

	// maybe not necessary; maybe it's okay to have a page mapped but
	// mprotected inacessible? That'd solve the zero-page problem nicely.
	// Nope, this didn't solve it, anyway.
#if 0 // dead code
	MemSlot *memslot = allocate_guest_memory(PAGE_SIZE, "page_zero");

	uint32_t *p, v;
	for (p=(uint32_t*) memslot->get_host_addr(), v=0x9900;
		(p+1) <= (uint32_t*) (memslot->get_host_addr()+memslot->get_size());
		p++, v++)
	{
		*p = v;
	}
#endif
}

MemSlot *ZoogVM::allocate_guest_memory(uint32_t req_size, const char *dbg_label)
{
	MemSlot *result = NULL;
	mutex->lock();

	uint32_t round_size = MemSlot::round_up_to_page(req_size);
	MemSlot *mem_slot = new MemSlot(dbg_label);
	Range guest_range;
	memory_map_mutex->lock();
	bool alloc_rc = guest_memory_allocator->allocate_range(
		round_size, mem_slot, &guest_range);
	memory_map_mutex->unlock();
	if (!alloc_rc)
	{
		delete mem_slot;
		result = NULL;
	}
	else
	{
		lite_assert(guest_range.end <= host_phys_memory_size);
		lite_assert(guest_range.size() == round_size);
		uint8_t *host_addr = host_phys_memory + guest_range.start;
		mem_slot->configure(guest_range, host_addr);

		// We allocate fresh memory, rather than "exposing" the
		// originally-mmaped PROT_NONE reserved region, because we
		// don't want to re-expose used memory. We want to always supply
		// zero memory. It's not actually required (since we're not leaking
		// any memory values that didn't already come from the app),
		// but we like having the check-for-zeros test in the app.

		// The mmapOverride lock here keeps other threads from snatching
		// this bit of guest address space between when we munmap it and
		// mmap it again. That would be BAD, because it might end up
		// making some of our host memory visible to the guest!
		// TODO security: the override isn't complete -- malloc'd memory
		// still isn't
		// caught, because we can't seem to pry our way under libc. Need
		// to do more research, or evict malloc and use mf_malloc instead.
		// The good news is that if an adversary wins this race, they have
		// to exploit it before we proceed to the assert following
		// te munmap, so "at least the window is short." Yeah, you can
		// just chisel that into my gravestone right now.
		//
		// TODO all this mmapOverride junk is obsolete; I'm now
		// using MAP_FIXED flag to mmap, which does the address replacement
		// atomically. Phooey for not noticing that option sooner.
#define REUSE_MEMORY_PAGES 0
#if REUSE_MEMORY_PAGES
		int mprotect_rc = mprotect(host_addr, round_size, PROT_READ|PROT_WRITE);
		lite_assert(mprotect_rc==0);
#else
		mmapOverride->lock();
#if 0
		int rc;
		rc = munmap(host_addr, round_size);
		lite_assert(rc==0);
#endif
		void *mmap_rc = mmap(host_addr, round_size,
			PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
		lite_assert(mmap_rc == host_addr);
		mmapOverride->unlock();
#endif
		result = mem_slot;
	}

	//dbg_dump_memory_map();
	mutex->unlock();
	return result;
}

void ZoogVM::free_guest_memory(MemSlot *mem_slot)
{
	free_guest_memory(mem_slot->get_guest_addr(), mem_slot->get_size());
}

void ZoogVM::free_guest_memory(uint32_t guest_start, uint32_t size)
{
	mutex->lock();
	// we may free more than the caller asked for, because we have page-size
	// granularity.
	uint32_t guest_start_rounded = MemSlot::round_down_to_page(guest_start);
	uint32_t guest_end_rounded = MemSlot::round_up_to_page(guest_start+size);
	memory_map_mutex->lock();
	Range guest_range = Range(guest_start_rounded, guest_end_rounded);
	if (guest_range.intersect(host_alarms_page->get_guest_range()).size() != 0)
	{
		safety_violation(__FILE__, __LINE__, "Guest freed a reserved page");
		lite_assert(false);
	}
	else
	{
		guest_memory_allocator->free_range(guest_range);
	}
	memory_map_mutex->unlock();

	//dbg_dump_memory_map();
	mutex->unlock();
}

uint32_t ZoogVM::map_image(uint8_t *image, uint32_t size, const char *dbg_label)
{
	MemSlot *code_slot = allocate_guest_memory(size, dbg_label);
	memcpy(code_slot->get_host_addr(), image, size);
	return (uint32_t) code_slot->get_guest_addr();
}

void ZoogVM::map_app_code(uint8_t *image, uint32_t size, const char *dbg_bootblock_path)
{
	lite_assert(guest_app_code_start == 0);
	guest_app_code_start = (void*) map_image(image, size, "app_code_image");
	this->dbg_bootblock_path=strdup(dbg_bootblock_path);
	lite_assert(guest_app_code_start != 0);
}

void ZoogVM::set_pub_key(ZPubKey *pub_key)
{
	this->pub_key = pub_key;
	coordinator->connect(pub_key);
	ZKeyPair *kp = coordinator->send_get_monitor_key_pair();
	crypto.set_monitorKeyPair(kp);
}

void ZoogVM::_resume_coordinator(ZPubKey *pub_key, XIPifconfig *ifconfigs)
{
	this->pub_key = pub_key;
	coordinator->reconnect(pub_key, ifconfigs);
	ZKeyPair *kp = coordinator->send_get_monitor_key_pair();
	crypto.set_monitorKeyPair(kp);
}

ZPubKey *ZoogVM::get_pub_key()
{
	return this->pub_key;
}

ZKeyPair *ZoogVM::get_monitorKeyPair()
{
	return crypto.get_monitorKeyPair();
}

KeyDerivationKey *ZoogVM::get_app_master_key()
{
	return crypto.get_app_master_key();
}

ZPubKey *ZoogVM::get_zoog_ca_root_key()
{
	return crypto.get_zoog_ca_root_key();
}

void ZoogVM::set_guest_entry_point(uint32_t entry_point_guest)
{
	this->entry_point_guest = entry_point_guest;
}

void ZoogVM::start()
{
	next_zid = 1;
	root_vcpu = new ZoogVCPU(this, entry_point_guest);

	while (1)
	{
		sleep(1);
		if (coredump_request_flag)
		{
			while (true)
			{
				char buf[50];
				fprintf(stderr, "Type \"core\" for corefile: ");
				fflush(stderr);
				char *s = fgets(buf, sizeof(buf), stdin);
				if (s!=NULL && strcmp(s, "core\n")==0)
				{
					break;
				}
			}

			void segv_load_all();
			segv_load_all();

			const char *coredump_fn = "zvm.core";
			FILE *fp = fopen(coredump_fn, "w");
			emit_corefile(fp);
			fclose(fp);
			coredump_request_flag = false;
			fprintf(stderr, "Dumped core to %s\n", coredump_fn);
		}
	}
}

void ZoogVM::dbg_dump_memory_map()
{
	memory_map_mutex->lock();
	ByStartTree *tree = guest_memory_allocator->dbg_peek_allocated_tree();
	RangeByStartElt *elt = tree->findMax();
	while (elt!=NULL)
	{
		MemSlot *ms = (MemSlot*) elt->get_user_obj();
		fprintf(stderr, "Guest 0x%08x Host 0x%08x Size 0x%08x %s\n",
			(uint32_t) ms->get_guest_addr(),
			(uint32_t) ms->get_host_addr(),
			ms->get_size(),
			ms->get_dbg_label()
			);
		elt = tree->findFirstLessThan(elt);
	}
	memory_map_mutex->unlock();
}

void ZoogVM::destroy()
{
	// Wish I knew how! Close the fd?
	fprintf(stderr, "Destroying VM.\n");
	close(vmfd);
	::_exit(0);
}

void ZoogVM::request_coredump()
{
	if (wait_for_core)
	{
		this->coredump_request_flag = true;
	}
	else
	{
		exit(-1);
	}
}

void ZoogVM::record_vcpu(ZoogVCPU *vcpu)
{
	mutex->lock();
	linked_list_insert_tail(&vcpus, vcpu);
	mutex->unlock();
}

void ZoogVM::remove_vcpu(ZoogVCPU *vcpu)
{
	mutex->lock();
	linked_list_remove(&vcpus, vcpu);
	mutex->unlock();
}

void ZoogVM::lock_memory_map()
{
	// LIVENESS: need to lock memory map (and boundscheck) here to
	// be sure we don't segfault ourselves trying to copy out memory
	// that the client has unmapped asynchronously.
	memory_map_mutex->lock();
}

void ZoogVM::unlock_memory_map()
{
	memory_map_mutex->unlock();
}

void *ZoogVM::map_region_to_host(uint32_t guest_start, uint32_t size)
{
	Range out_range;
	MemSlot *mem_slot = (MemSlot*)
		guest_memory_allocator->lookup_value(guest_start, &out_range);
	if (mem_slot==NULL)
	{
		return NULL;
	}

	lite_assert(guest_start >= mem_slot->get_guest_addr());
	if (guest_start+size < mem_slot->get_guest_addr()+mem_slot->get_size())
	{
		return mem_slot->get_host_addr() +
			(guest_start - mem_slot->get_guest_addr());
	}
	return NULL;
}

void ZoogVM::_pause_all()
{
	LinkedListIterator lli;
	for (ll_start(&vcpus, &lli);
		ll_has_more(&lli);
		ll_advance(&lli))
	{
		ZoogVCPU *vcpu = (ZoogVCPU *) ll_read(&lli);
		vcpu->pause();
	}
}

void ZoogVM::_resume_all()
{
	LinkedListIterator lli;
	for (ll_start(&vcpus, &lli);
		ll_has_more(&lli);
		ll_advance(&lli))
	{
		ZoogVCPU *vcpu = (ZoogVCPU *) ll_read(&lli);
		vcpu->resume();
	}
}

void ZoogVM::emit_corefile(FILE *fp)
{
	CoreFile c;
	corefile_init(&c, mf);

	_pause_all();

	fprintf(stderr, "Core includes %d threads\n", vcpus.count);
	// Threads
	LinkedListIterator lli;
	for (ll_start(&vcpus, &lli);
		ll_has_more(&lli);
		ll_advance(&lli))
	{
		ZoogVCPU *vcpu = (ZoogVCPU *) ll_read(&lli);
		Core_x86_registers regs;
		vcpu->get_registers(&regs);
		corefile_add_thread(&c, &regs, vcpu->get_zid());
	}

	// Memory
	lock_memory_map();
	Range this_range;
	MemSlot *slot;
	for (slot = (MemSlot*) guest_memory_allocator->first_range(&this_range);
		slot != NULL;
		slot = (MemSlot*) guest_memory_allocator->next_range(this_range, &this_range))
	{
		corefile_add_segment(&c, slot->get_guest_addr(), slot->get_host_addr(), slot->get_size());
	}
	corefile_set_bootblock_info(&c, get_guest_app_code_start(), dbg_bootblock_path);

	corefile_write(fp, &c);

	unlock_memory_map();

	_resume_all();
}

void ZoogVM::_emit_corefile(FILE *fp)
{
	CoreFile c;
	corefile_init(&c, mf);

	fprintf(stderr, "Core includes %d threads\n", vcpus.count);
	// Threads
	LinkedListIterator lli;
	for (ll_start(&vcpus, &lli);
		ll_has_more(&lli);
		ll_advance(&lli))
	{
		ZoogVCPU *vcpu = (ZoogVCPU *) ll_read(&lli);
		Core_x86_registers regs;
		vcpu->get_registers(&regs);
		corefile_add_thread(&c, &regs, vcpu->get_zid());
	}

	// Memory
	Range this_range;
	MemSlot *slot;
	for (slot = (MemSlot*) guest_memory_allocator->first_range(&this_range);
		slot != NULL;
		slot = (MemSlot*) guest_memory_allocator->next_range(this_range, &this_range))
	{
		fprintf(stderr, "liang: [LABEL:%s] get_guest_addr=%x, get_host_addr=%x, get_size=%d\n",
			slot->get_dbg_label(), slot->get_guest_addr(), 
			(int)slot->get_host_addr(), slot->get_size());
		corefile_add_segment(&c, slot->get_guest_addr(), slot->get_host_addr(), slot->get_size());
	}
	fprintf(stderr, "liang: get_guest_app_code_start=%x, dbg_bootblock_path=%s\n",
		(int)get_guest_app_code_start(), dbg_bootblock_path);
	corefile_set_bootblock_info(&c, get_guest_app_code_start(), dbg_bootblock_path);

	corefile_write(fp, &c);
}

void ZoogVM::_emit_swapfile(FILE *fp)
{
	int rc; 
	struct swap_file_header header;
	struct swap_thread thread;
	struct swap_vm vm;

	vm.guest_app_code_start = guest_app_code_start;
	strcpy(vm.dbg_bootblock_path, dbg_bootblock_path);

	header.thread_count = vcpus.count;
	vm.host_alarms_page_guest_addr = host_alarms_page->get_guest_addr();
	vm.idt_page_guest_addr = idt_page->get_guest_addr();
	int num_ifconfigs;
	XIPifconfig *ifconfigs = coordinator->get_ifconfigs(&num_ifconfigs);
	vm.ifconfigs[0] = ifconfigs[0];
	vm.ifconfigs[1] = ifconfigs[1];
	vm.pub_key_size = this->pub_key->size();
	header.swap_vm_size = sizeof(vm) + vm.pub_key_size;

	rc = fwrite(&header, sizeof(header), 1, fp);
	rc = fwrite(&vm, sizeof(vm), 1, fp);
	// save pub_key
	uint8_t buffer[vm.pub_key_size];
	this->pub_key->serialize(buffer);
	rc = fwrite(buffer, vm.pub_key_size, 1, fp);

	LinkedListIterator lli;
	for (ll_start(&vcpus, &lli);
		ll_has_more(&lli);
		ll_advance(&lli))
	{
		ZoogVCPU *vcpu = (ZoogVCPU *) ll_read(&lli);
		vcpu->get_swap_thread(&thread);
		rc = fwrite(&thread, sizeof(thread), 1, fp);
		fprintf(stderr, "guest_entry_point=%x, stack_top_guest=%x, gdt_page_guest_addr=%x\n", 
			thread.guest_entry_point, thread.stack_top_guest, thread.gdt_page_guest_addr);
	}
}

void ZoogVM::checkpoint()
{
	const char *corefile = "kvm.core";
	const char *swapfile = "kvm.swap";

	fprintf(stderr, "Checkpointing...\n");
	FILE *fp;

	_pause_all();
	lock_memory_map();
	
	fp = fopen(corefile, "w+");
	lite_assert(fp!=NULL);
	_emit_corefile(fp);
	fclose(fp);
	
	fp = fopen(swapfile, "w+");
	lite_assert(fp!=NULL);
	_emit_swapfile(fp);
	fclose(fp);

	// unlock_memory_map();
	fprintf(stderr, "Checkpointing DONE.\n");
	exit(0);
}

void ZoogVM::resume()
{
	_resume_all();
	while (1)
	{
		sleep(1);
		if (coredump_request_flag)
		{
			while (true)
			{
				char buf[50];
				fprintf(stderr, "Type \"core\" for corefile: ");
				fflush(stderr);
				char *s = fgets(buf, sizeof(buf), stdin);
				if (s!=NULL && strcmp(s, "core\n")==0)
				{
					break;
				}
			}

			const char *coredump_fn = "zvm.core";
			FILE *fp = fopen(coredump_fn, "w");
			emit_corefile(fp);
			fclose(fp);
			coredump_request_flag = false;
			fprintf(stderr, "Dumped core to %s\n", coredump_fn);
		}
	}
}

#define DYNAMIC_MAPPER false

typedef struct
{
	uint8_t *host_addr;
	uint32_t virt_addr;
	uint32_t size;
	long fp_offset;
	int prot;
} Mmapper;

Mmapper *mmapper_list;
int mmaper_list_count = 0;

void segv_load_all()
{
	Mmapper *mp;
	int rc;
	int mmap_prot = PROT_READ | PROT_WRITE;

	const char *corefile = "kvm.core";
	FILE *fp = fopen(corefile, "r");
	lite_assert(fp!=NULL);

	for (int i = 0; i < mmaper_list_count; i++) {
		mp = mmapper_list + i;
		if(mp->prot == PROT_NONE) {
			mp->prot = mmap_prot;
			void *mmap_rc = mmap(mp->host_addr, mp->size, mmap_prot, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
			lite_assert(mmap_rc == mp->host_addr);

			fseek(fp, mp->fp_offset, SEEK_SET);
			rc = fread(mp->host_addr, mp->size, 1, fp);
			lite_assert(rc == 1);
			// lite_assert((int)(mp->fp_offset + mp->size) == ftell(fp));
			
			fprintf(stderr, "Load from disk: guest_addr=%x, host_addr=%x, size=%u\n",
				mp->virt_addr, (unsigned int)mp->host_addr, mp->size);
		}
	}
	fclose(fp);
}

void segv_load_addr(void *segv_addr)
{
	// void *pg = (void*)((long)(segv_addr) & ~4095);

	Mmapper *mp;
	for (int i = 0; i < mmaper_list_count; i++) {
		mp = mmapper_list + i;
		if (mp->host_addr <= segv_addr && 
			  segv_addr < mp->host_addr + mp->size) {
			break;
		}
	}

	int mmap_prot = PROT_READ | PROT_WRITE;
	if(mp->prot == PROT_NONE) {
		int rc;
		// if (mprotect(mp->host_addr, mp->size, mmap_prot) <0){
		// 	fprintf(stderr, "fault: can't mprotect.\n");
		// 	exit(1);
		// }
		void *mmap_rc = mmap(mp->host_addr, mp->size, mmap_prot, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
		lite_assert(mmap_rc == mp->host_addr);

		mp->prot = mmap_prot;
		const char *corefile = "kvm.core";
		FILE *fp = fopen(corefile, "r");
		lite_assert(fp!=NULL);
		fseek(fp, mp->fp_offset, SEEK_SET);
		rc = fread(mp->host_addr, mp->size, 1, fp);
		lite_assert(rc == 1);
		// lite_assert((int)(mp->fp_offset + mp->size) == ftell(fp));
		fclose(fp);
		fprintf(stderr, "Load from disk: guest_addr=%x, host_addr=%x, size=%u\n",
			mp->virt_addr, (unsigned int)mp->host_addr, mp->size);
	}

	// int mmap_size = 4096;
	// fprintf(stderr, "fault at %p (%p), set mode to %x\n", segv_addr, pg, mmap_prot);
	// if (mprotect(pg, mmap_size, mmap_prot) <0){
	// 	perror("fault: can't mprotect.");
	// 	exit(1);
	// }
	// assert(memcpy(desired_host_phys_addr, data, strlen(data)));
}

void segv_handler(int signum, siginfo_t *siginfo, void *context)
{
	void *pg = (void*)((long)(siginfo->si_addr) & ~4095);
	fprintf(stderr, "fault at %p (%p)\n", siginfo->si_addr, pg);

	segv_load_addr(siginfo->si_addr);
}

void set_segv_handler()
{
	int rc;
	struct sigaction sa;
	sa.sa_sigaction = segv_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO;
	rc = sigaction(SIGSEGV, &sa, NULL);
	lite_assert(rc == 0);
}

void ZoogVM::_load_swap(const char *core_file, struct swap_vm **out_vm)
{
	const char *corefile = "kvm.core";
	const char *swapfile = "kvm.swap";

	fprintf(stderr, "Resume from core file: %s\n", core_file);

	FILE *fp = fopen(corefile, "r");
	lite_assert(fp!=NULL);

	FILE *fp_swap = fopen(swapfile, "r");
	lite_assert(fp!=NULL);

	// CoreFile c;
	// corefile_read(fp, &c);
	int rc; 
	int pos = 0; // track the last phdr was read
	int i, j;
	int num_notes = 1;
	int thread_notes_size;
	int thread_offset;
	static const char *label = "";

	struct swap_file_header header;
	
	// load header
	rc = fread(&header, sizeof(header), 1, fp_swap);

	// load swap_vm, make sure free this memory
	uint8_t *vm_buf = new uint8_t[header.swap_vm_size];//[header.swap_vm_size];
	rc = fread(vm_buf, header.swap_vm_size, 1, fp_swap);
	struct swap_vm *vm = (struct swap_vm *)vm_buf;
	*out_vm = vm;

	this->guest_app_code_start = vm->guest_app_code_start;
	this->dbg_bootblock_path = strdup(vm->dbg_bootblock_path);

	// load pub_key
	this->pub_key = new ZPubKey(vm->pub_key_serialized, vm->pub_key_size);

	// load threads
	uint8_t buf[header.thread_count * sizeof(struct swap_thread_extra)];
	struct swap_thread_extra *ptr_thread = (struct swap_thread_extra *)buf;
	for (i = 0; i < (int)header.thread_count; i ++) {
		rc = fread(ptr_thread+i, sizeof(struct swap_thread), 1, fp_swap);
	}

	Elf32_Ehdr ehdr;
	rc = fread(&ehdr, sizeof(Elf32_Ehdr), 1, fp);
	lite_assert(rc == 1);
	pos += sizeof(ehdr);
	lite_assert(pos == ftell(fp));

	// Read CoreNotes
	Elf32_Phdr phdr;
	rc = fread(&phdr, sizeof(Elf32_Phdr), 1, fp);
	lite_assert(rc == 1);
	pos += sizeof(phdr);
	lite_assert(pos == ftell(fp));
	thread_notes_size = phdr.p_filesz - sizeof(CoreNote_Zoog);
	thread_offset = phdr.p_offset;

	int mem_header_count = ehdr.e_phnum - num_notes;
	if (DYNAMIC_MAPPER)
	{
		mmapper_list = (Mmapper*)mf_malloc(mf, sizeof(Mmapper) * mem_header_count);
		mmaper_list_count = mem_header_count;
		set_segv_handler();
	}

	uint32_t last_guest_range_end = 0x10001000; // the same as _map_physical_memory
	for (i = 0; i < mem_header_count; i++)
	{
		rc = fread(&phdr, sizeof(Elf32_Phdr), 1, fp);
		lite_assert(rc == 1);
		pos += sizeof(phdr);
		lite_assert(pos == ftell(fp));
		
		{
			uint32_t size = phdr.p_memsz;
			
			MemSlot *mem_slot = new MemSlot(label);
			Range guest_range;
			uint32_t round_size = size;
			guest_memory_allocator->allocate_range_at(phdr.p_vaddr, round_size, mem_slot, &guest_range);
			uint8_t *host_addr = host_phys_memory + guest_range.start;
			mem_slot->configure(guest_range, host_addr);

			int mmap_prot = PROT_READ|PROT_WRITE;
			if (DYNAMIC_MAPPER)
			{
				mmap_prot = PROT_NONE;
				mmapper_list[i].host_addr = host_addr;
				mmapper_list[i].size = round_size;
				mmapper_list[i].virt_addr = phdr.p_vaddr;
				mmapper_list[i].fp_offset = phdr.p_offset;
				mmapper_list[i].prot = mmap_prot;
			}

			void *mmap_rc = mmap(host_addr, round_size, mmap_prot, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
			lite_assert(mmap_rc == host_addr);

			if (!DYNAMIC_MAPPER) {
				fseek(fp, phdr.p_offset, SEEK_SET);
				fread(host_addr, size, 1, fp);
				lite_assert((int)(phdr.p_offset + size) == ftell(fp));
			}

			fprintf(stderr, "Memory loaded: get_guest_addr=%x, get_host_addr=%x, get_size=%d\n", 
				mem_slot->get_guest_addr(), (int)mem_slot->get_host_addr(), mem_slot->get_size());

			// liang TODO: improve efficiency 
			if(guest_range.start == vm->host_alarms_page_guest_addr) {
				host_alarms_page = mem_slot;
			} else if (guest_range.start == vm->idt_page_guest_addr) {
				idt_page = mem_slot;
			} else {
				// linear search :(
				for(j = 0; j < (int)header.thread_count; j++){
					if ((ptr_thread+j)->thread.gdt_page_guest_addr == guest_range.start) {
						(ptr_thread+j)->gdt_page = mem_slot;
					}
				}
			}

			// core dump memory pages should sort by start addresses
			lite_assert(guest_range.start >= last_guest_range_end);
			if (guest_range.start > last_guest_range_end) {
				// there is a gap, let's make a free block
				guest_memory_allocator->force_insert_coalesced_free_range(
					Range(last_guest_range_end, guest_range.start));
			}
			last_guest_range_end = guest_range.end;

			fseek(fp, pos, SEEK_SET);
		}

	}
	if (last_guest_range_end < host_phys_memory_size){
		// guest_memory_allocator->create_empty_range(
		// 	Range(last_guest_range_end, host_phys_memory_size));
		// liang: use this for performance, but it will not coalescing adjacent free blocks.
		guest_memory_allocator->force_insert_coalesced_free_range(
			Range(last_guest_range_end, host_phys_memory_size));
	}
	lite_assert(host_alarms_page != NULL);
	lite_assert(idt_page != NULL);

	// while (pos < thread_offset + thread_notes_size)
	// {
	// 	CoreNote_Regs corenote_regs;
	// 	rc = fread(&corenote_regs, sizeof(CoreNote_Regs), 1, fp);
	// 	ZoogVCPU *vcpu = new ZoogVCPU(this, corenote_regs.desc.regs.ip, corenote_regs.desc.regs.sp);
	// 	vcpu->get_zid();
	// }

	struct swap_thread_extra *ptr;
	for(ptr = ptr_thread; ptr < ptr_thread + header.thread_count; ptr++) {
		fprintf(stderr, "Thread: guest_entry_point=%x, stack_top_guest=%x, gdt_page_guest_addr=%x\n", 
			ptr->thread.guest_entry_point, ptr->thread.stack_top_guest, ptr->thread.gdt_page_guest_addr);
		ZoogVCPU *vcpu = new ZoogVCPU(this, ptr);
		vcpu->get_zid(); 
		lite_assert(ptr->gdt_page);
	}

	if (DYNAMIC_MAPPER) {
		// void *desired_host_phys_addr = (void*) 0x18000000;
		// rc = mprotect(desired_host_phys_addr, host_phys_memory_size, PROT_NONE);
		// lite_assert(rc==0);
		segv_load_addr((void*)0x28003000);
		segv_load_addr((void*)0x28153000);	
	}

	fclose(fp);
	fclose(fp_swap);
}
