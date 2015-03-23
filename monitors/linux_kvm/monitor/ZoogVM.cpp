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
#include <err.h>
#include "uvmem/uvmem.h"
#include <sys/prctl.h>

// liang: print page hash value
#include <openssl/sha.h>

#include "pyhelper.h"

#include "ZoogVM.h"
#include "ZoogVCPU.h"
#include "MemSlot.h"
#include "SyncFactory_Pthreads.h"
#include "corefile.h"
#include "safety_check.h"
#include "VCPUPool.h"
#include "KeyDerivationKey.h"

#define debug_printf(format, ...) fprintf (stderr, format, ## __VA_ARGS__)
// #define debug_printf(format, ...)

ZoogVM::ZoogVM(MallocFactory *mf, MmapOverride *mmapOverride, MonitorArgs *margs)
	: crypto(MonitorCrypto::NO_MONITOR_KEY_PAIR)
{
	this->mf = mf;
	this->monitor_args = margs;
	this->sf = new SyncFactory_Pthreads();
	this->mmapOverride = mmapOverride;
	this->wait_for_core = margs->wait_for_core;
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

	if (!margs->is_resume) { // Normal start
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

		assigned_ifconfigs = NULL;
	} else { 
		// liang: swapon
		// resume from checkpoint
		_setup(true);

		struct swap_vm *vm;
		_load_swap(margs->swap_file, &vm);

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

		delete vm; // make sure recycle it, bad design, TODO: refactory here.
	}
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

	void *desired_host_phys_addr = (void*) HOST_ADDR_START;
	host_phys_memory = (uint8_t*) mmap(
		desired_host_phys_addr, host_phys_memory_size,
		PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	lite_assert(host_phys_memory != MAP_FAILED);
	lite_assert(host_phys_memory == desired_host_phys_addr);

	if(!resume)
		guest_memory_allocator->create_empty_range(Range(GUEST_ADDR_START, host_phys_memory_size));

	struct kvm_userspace_memory_region umr;
	umr.slot = 0;
	// umr.flags = 0;
	// liang: enable dirty page log.
	umr.flags = KVM_MEM_LOG_DIRTY_PAGES;
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
	// liang: connect with assigned address if provided.
	if (assigned_ifconfigs) {
		coordinator->reconnect(pub_key, assigned_ifconfigs);
	} else {
		coordinator->connect(pub_key);
	}
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

			// void segv_load_all();
			// segv_load_all();

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

void ZoogVM::set_swapfile(const char *filename)
{
	int len = strlen(filename);
	swapfile = (char*)malloc(len+1);
	if(!swapfile)
		err(EXIT_FAILURE, "cannot allocate space for swapfile.");
	memcpy(swapfile, filename, len+1);
	const char *page_suffix = ".page";
	int len2 = strlen(page_suffix);
	pagefile = (char*)malloc(len+len2+1);
	if(!pagefile)
		err(EXIT_FAILURE, "cannot allocate space for pagefile.");
	memcpy(pagefile, filename, len);
	memcpy(pagefile+len, page_suffix, len2+1);
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

	debug_printf("Core includes %d threads\n", vcpus.count);
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
		debug_printf("liang: [LABEL:%s] get_guest_addr=%x, get_host_addr=%x, get_size=%d\n",
			slot->get_dbg_label(), slot->get_guest_addr(), 
			(int)slot->get_host_addr(), slot->get_size());
		corefile_add_segment(&c, slot->get_guest_addr(), slot->get_host_addr(), slot->get_size());
	}
	debug_printf("liang: get_guest_app_code_start=%x, dbg_bootblock_path=%s\n",
		(int)get_guest_app_code_start(), dbg_bootblock_path);
	corefile_set_bootblock_info(&c, get_guest_app_code_start(), dbg_bootblock_path);

	corefile_write(fp, &c);
}

void ZoogVM::_emit_swapfile()
{
	int rc; 
	struct swap_file_header header;
	struct swap_thread thread;
	struct swap_vm vm;

	FILE *fp_swap = fopen(swapfile, "w+");
	lite_assert(fp_swap!=NULL);

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
	header.segment_count = guest_memory_allocator->get_range_count();

	rc = fwrite(&header, sizeof(header), 1, fp_swap);
	rc = fwrite(&vm, sizeof(vm), 1, fp_swap);
	// save pub_key
	uint8_t buffer[vm.pub_key_size];
	this->pub_key->serialize(buffer);
	rc = fwrite(buffer, vm.pub_key_size, 1, fp_swap);

	LinkedListIterator lli;
	for (ll_start(&vcpus, &lli);
		ll_has_more(&lli);
		ll_advance(&lli))
	{
		ZoogVCPU *vcpu = (ZoogVCPU *) ll_read(&lli);
		vcpu->get_swap_thread(&thread);
		rc = fwrite(&thread, sizeof(thread), 1, fp_swap);
		debug_printf("guest_entry_point=%x, stack_top_guest=%x, gdt_page_guest_addr=%x\n", 
			thread.guest_entry_point, thread.stack_top_guest, thread.gdt_page_guest_addr);
	}

	// Memory
	int len = strlen(pagefile);
	const char *tmp_suffix = ".tmp";
	int len2 = strlen(tmp_suffix);
	char pagefile_tmp[256];
	memcpy(pagefile_tmp, pagefile, len);
	memcpy(pagefile_tmp+len, tmp_suffix, len2+1);

	FILE *fp_page = fopen(pagefile_tmp, "w+");
	lite_assert(fp_page != NULL);

	uint32_t seg_count = 0;
	Range this_range;
	struct swap_segment seg;
	MemSlot *slot;
	uint32_t offset = 0;
	uint32_t last_guest_range_end = GUEST_ADDR_START;
	for (slot = (MemSlot*) guest_memory_allocator->first_range(&this_range);
		slot != NULL;
		slot = (MemSlot*) guest_memory_allocator->next_range(this_range, &this_range))
	{
		seg_count++;
		seg.vaddr = slot->get_guest_addr();
		seg.size = slot->get_size();
		seg.offset = offset;
		offset += seg.size;
		last_guest_range_end = this_range.end;
		rc = fwrite(&seg, sizeof(seg), 1, fp_swap);

		rc = fwrite(slot->get_host_addr(), seg.size, 1, fp_page);
	}
	lite_assert(seg_count == header.segment_count);

	fclose(fp_page);
	fclose(fp_swap);

	rename(pagefile_tmp, pagefile);
}

#if 0
void sync_ckpt_mmap()
{
	// liang: use this function in testing write back on file-backed pages.
	// result: KVM write back don't work, because cannot map shared pages.
	int rc;
	rc = msync((void*)0x28001000, 69763072, MS_SYNC);
	lite_assert(rc == 0);
	debug_printf("Sync to mapped file.\n");
}
#endif

#define HASH_CHAR_LENGTH (SHA_DIGEST_LENGTH*2+1)
char *get_hash_str(const unsigned char *d, size_t n, char *hash_s)
{
	unsigned char hash[SHA_DIGEST_LENGTH];
	unsigned char *rc = SHA1(d, n, hash);
	lite_assert(rc);
	for(int i = 0; i < SHA_DIGEST_LENGTH; i++) {
		sprintf(hash_s+i*2, "%02X", hash[i]);
	}
	return hash_s;
}

void read_dirty_log(uint8_t *bitmap, int size)
{
	int i = 0, p;
	unsigned long val = 0;
	long page_start = 0;//GUEST_ADDR_START;
	unsigned long page = 0, count = 0, print_cut = 100;
	while (i < size) {
		// long in 32bit process is 4 bytes while it's 8 bytes in 64bit.
		// I am using 64bit kernel, and kernel use long type for bitmap ops.
		if (i + (int)sizeof(long) > size) {
			lite_assert(memcpy(&val, bitmap+i, size-i) == &val);
		} else {
			val = *(long*)(bitmap+i);
		}

		while((p = ffsl(val))) {
			page = page_start + (i*8+p-1)*PAGE_SIZE;
			unsigned long host_page = page + HOST_ADDR_START;
			if (count < print_cut) {
#if PRINT_PAGE_HASH
				char hash_s[HASH_CHAR_LENGTH];
				get_hash_str((const unsigned char*)host_page, PAGE_SIZE, hash_s);
				printf("Byte offset %d, updated virtual page 0x%lx, host page 0x%lx, hash %s\n", i, page, host_page, hash_s);				
#else
				printf("Byte offset %d, updated virtual page 0x%lx, host page 0x%lx\n", i, page, host_page);
#endif
			}
			else if (count == print_cut) {
				printf("This section has updated more than %ld pages.\n", print_cut);
			}
			val = val & ~(1UL << (p-1));
			count++;
		}
		i += sizeof(long);
	}
	printf("Allocated %d bytes bitmap and updated %ld pages.\n", size, count);
}

void print_dirty_log(int vmfd)
{
	// sync_ckpt_mmap();
	int size = HOST_PHYS_MEM_SIZE;
	// size = 0x11389000 + 4096*1;
	int nbyte = ((GUEST_ADDR_START + size-1) / PAGE_SIZE / 8) + 1;
	// uint8_t *dlog_buf = (uint8_t*)mf_malloc(mf, nbyte);
	uint8_t *dlog_buf = (uint8_t*)malloc(nbyte);
	lite_assert(dlog_buf);
	// uint8_t dlog_buf[nbyte];
	memset(dlog_buf, 0, nbyte);

	struct kvm_dirty_log dlog;
	dlog.slot = 0; // liang: we only have one mapped region/slot
	// dlog.dirty_bitmap = &dlog_buf;
	dlog.dirty_bitmap = dlog_buf;
	int rc = ioctl(vmfd, KVM_GET_DIRTY_LOG, &dlog);
	lite_assert(rc == 0);
	read_dirty_log((uint8_t*)dlog.dirty_bitmap, nbyte);
	// mf_free(mf, dlog_buf);
	free(dlog_buf);
}

void ZoogVM::checkpoint()
{
	// print_dirty_log(vmfd);
	// return; 

	// const char *corefile = "kvm.core";
	// const char *swapfile = "kvm.swap";

	debug_printf("Checkpointing...\n");

	_pause_all();
	lock_memory_map();
	
	// fp = fopen(corefile, "w+");
	// lite_assert(fp!=NULL);
	// _emit_corefile(fp);
	// fclose(fp);
	
	// fp = fopen(swapfile, "w+");
	// lite_assert(fp!=NULL);
	_emit_swapfile();
	// fclose(fp);

	// unlock_memory_map();
	debug_printf("Checkpointing DONE.\n");
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
				debug_printf("Type \"core\" for corefile: ");
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
			debug_printf("Dumped core to %s\n", coredump_fn);
		}
	}
}

#define DYNAMIC_MAPPER true

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

#if 0
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
			
			debug_printf("Load from disk: guest_addr=%x, host_addr=%x, size=%u\n",
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
		// 	debug_printf("fault: can't mprotect.\n");
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
		debug_printf("Load from disk: guest_addr=%x, host_addr=%x, size=%u\n",
			mp->virt_addr, (unsigned int)mp->host_addr, mp->size);
	}

	// int mmap_size = 4096;
	// debug_printf("fault at %p (%p), set mode to %x\n", segv_addr, pg, mmap_prot);
	// if (mprotect(pg, mmap_size, mmap_prot) <0){
	// 	perror("fault: can't mprotect.");
	// 	exit(1);
	// }
	// assert(memcpy(desired_host_phys_addr, data, strlen(data)));
}

void segv_handler(int signum, siginfo_t *siginfo, void *context)
{
	void *pg = (void*)((long)(siginfo->si_addr) & ~4095);
	debug_printf("fault at %p (%p)\n", siginfo->si_addr, pg);

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
#endif

#define UVMEM_PAGE_BUF_SIZE 32

static int mmapper_page_comp(const void *m1, const void *m2)
{
	Mmapper *mp1 = (Mmapper*)m1, *mp2 = (Mmapper*)m2;
	if (mp1->host_addr < mp2->host_addr) {
		return mp1->host_addr - mp2->host_addr;
	} else if (mp1->host_addr > (mp2->host_addr+mp2->size-1)) {
		return mp1->host_addr - (mp2->host_addr+mp2->size-1);
	} else {
		if (mp2->host_addr <= mp1->host_addr &&
			mp1->host_addr < mp2->host_addr+mp2->size)
			return 0;
		else {
			// break page assumption
			// assuming each saved memory is at page boundary.
			err(EXIT_FAILURE, "Break page assumption.");
			return 0;
		}
	}
}

long find_page_file_offset(uint8_t *vaddr)
{
	Mmapper *mp = NULL; 
	void *segv_addr = (void*)(vaddr + HOST_ADDR_START);
	// debug_printf("find_page_file_offset(segv_addr=0x%x)\n", (unsigned int)(segv_addr));
	Mmapper key; key.host_addr = (uint8_t*)segv_addr;
	mp = (Mmapper*)bsearch(&key, mmapper_list, mmaper_list_count, sizeof(key), mmapper_page_comp);
	if (mp) {
		long fp_offset = mp->fp_offset + (long)((uint8_t*)segv_addr - mp->host_addr);
		return fp_offset;
	}
	return -1;
}

void load_page_at(FILE *fp, void *shmem, uint64_t pg, size_t page_size)
{
	printf("load_page_at(fp=%x, shmem=%x, pg=%lu)\n", (unsigned int)fp, (unsigned int)shmem, (long unsigned int)pg);
	int rc;
	Mmapper *mp = NULL; bool found = false;
	void *segv_addr = (void*)(HOST_ADDR_START + GUEST_ADDR_START + pg*page_size);

#if 0
	// Linear search.
	for (int j = 0; j < mmaper_list_count; j++) {
		mp = mmapper_list + j;
		if (mp->host_addr <= segv_addr && 
			  segv_addr < mp->host_addr + mp->size) {
			found = true;
			break;
		}
	}
#else
	Mmapper key; key.host_addr = (uint8_t*)segv_addr;
	mp = (Mmapper*)bsearch(&key, mmapper_list, mmaper_list_count, sizeof(key), mmapper_page_comp);
#endif
	found = (mp != NULL);
	if (found) {
		long fp_offset = mp->fp_offset + (long)((uint8_t*)segv_addr - mp->host_addr);
		rc = fseek(fp, fp_offset, SEEK_SET);
		lite_assert(rc == 0);
		debug_printf("load_page_at.fread(host_addr=%x)\n", (unsigned int)((uint8_t*)shmem+pg*page_size));
		rc = fread((uint8_t*)shmem+pg*page_size, page_size, 1, fp);
		lite_assert(rc);

#if PRINT_PAGE_HASH
		char hash_s[HASH_CHAR_LENGTH];
		get_hash_str((uint8_t*)shmem+pg*page_size, page_size, hash_s);
		debug_printf("page 0x%lx, host 0x%lx, hash %s\n", (unsigned long)pg, (unsigned long)segv_addr, hash_s);
#else
		debug_printf("page 0x%lx, host 0x%lx\n", (unsigned long)pg, (unsigned long)segv_addr);
#endif

	} else {
		memset((uint8_t*)shmem+pg*page_size, 0, page_size);
		debug_printf("page 0x%lx, host 0x%lx, page not found filling zeros\n", (unsigned long)pg, (unsigned long)segv_addr);
	}
}

#define USE_PYPAGE_LOADER 1

#ifndef USE_PYPAGE_LOADER
void serve_uvmem_pages(FILE *fp, int uvmem_fd, int shmem_fd, size_t size, size_t page_size)
{
	uint64_t buf_pgs[UVMEM_PAGE_BUF_SIZE];
	int len, n_pages=0, nr, i;

	int nr_pages = size / page_size;
	void* shmem = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
			   shmem_fd, 0);
	if (shmem == MAP_FAILED) {
		err(EXIT_FAILURE, "server: mmap(\"shmem\")");
	}
	close(shmem_fd);
	
	debug_printf("Serving pages at %x\n", (uint32_t)shmem);

	while (n_pages < nr_pages) {
		debug_printf("read(uvmem_fd)\n");
		len = read(uvmem_fd, buf_pgs, sizeof(buf_pgs));
		if (len < 0) {
			err(EXIT_FAILURE, "server: read");
		}
		nr = len / sizeof(buf_pgs[0]);
		debug_printf("Requesting %d pages.\n", nr);
		for (i = 0; i < nr; i++) {
			debug_printf("Request[%d] ", i);
			load_page_at(fp, shmem, buf_pgs[i], page_size);
		}
		int written = write(uvmem_fd, buf_pgs, len);
		if (written < len) {
			err(EXIT_FAILURE, "server: write");
		}
		n_pages += nr;
	}
	debug_printf("Exit uvmem serving loop.\n");
	munmap(shmem, size);
	close(shmem_fd);
	close(uvmem_fd);
	// exit(0);
}
#endif

void *serve_uvmem_pages_thread(void *arg)
{
	struct uvmem_server_arg *sarg = (struct uvmem_server_arg*)arg;
#if USE_PYPAGE_LOADER
	py_serve_uvmem_page(sarg);
#else
	serve_uvmem_pages(sarg->fp, sarg->uvmem_fd, sarg->shmem_fd, sarg->size, sarg->page_size);
#endif
	return NULL;
}

void init_uvmem(ZoogVM *zvm, FILE *fp, uint32_t last_guest_range_end)
{
	// liang: use uvmem to load pages on demand
	int uvmem_fd = open(DEV_UVMEM, O_RDWR);
	if (uvmem_fd < 0) {
		perror("can't open "DEV_UVMEM);
		exit(EXIT_FAILURE);
	}
	long page_size = sysconf(_SC_PAGESIZE);
	debug_printf("_SC_PAGESIZE = %ld\n", page_size);
	int round_size = MemSlot::round_up_to_page(last_guest_range_end);
	int npages = (round_size - GUEST_ADDR_START) / page_size;
	// struct uvmem_init uinit = {
	// 	.size = npages * page_size,
	// 	.padding = 0,
	// 	.shmem_fd = 0
	// };
	struct uvmem_init uinit;
	uinit.size = npages * page_size;
	uinit.padding = 0;
	uinit.shmem_fd = 0;
	if (ioctl(uvmem_fd, UVMEM_INIT, &uinit) < 0) {
		err(EXIT_FAILURE, "UVMEM_INIT");
	}

	int shmem_fd = uinit.shmem_fd;
	size_t uvmem_size = uinit.size;
	lite_assert(uvmem_size == (size_t)(npages * page_size));
	if (ftruncate(shmem_fd, uvmem_size) < 0) {
		err(EXIT_FAILURE, "truncate(\"shmem_fd\")");
	}
	debug_printf("uvmem_fd %d shmem_fd %d\n", uvmem_fd, shmem_fd);
	fflush(stdout);

#if 1
	// fork style uvmem processes
	pid_t child = fork();
	if (child < 0) {
		err(EXIT_FAILURE, "fork");
	} else if (child == 0) {
		// page server process
		// unmap vm pages
		int rc;
		rc = munmap((void*)HOST_ADDR_START, HOST_PHYS_MEM_SIZE);
		if (rc) {
			err(EXIT_FAILURE, "Fail munmap guest-host memory mapping region.");
			lite_assert(rc == 0);
		}
		close(zvm->get_kvmfd());
		close(zvm->get_vmfd());
		prctl(PR_SET_PDEATHSIG, SIGKILL); // kill child when parent exits

		struct uvmem_server_arg sarg;
		sarg.fp = fp;
		sarg.uvmem_fd = uvmem_fd;
		sarg.shmem_fd = shmem_fd;
		sarg.size = uvmem_size;
		sarg.page_size = page_size;
		sarg.pico_id = zvm->monitor_args->pico_id;
		serve_uvmem_pages_thread(&sarg);
		// serve_uvmem_pages(fp, uvmem_fd, shmem_fd, uvmem_size, page_size);
	} else {
		debug_printf("uvmem server process id %d\n", child);
		void *ram = mmap((void*)(HOST_ADDR_START+GUEST_ADDR_START), uvmem_size, PROT_READ | PROT_WRITE, MAP_PRIVATE|MAP_FIXED,
		 			uvmem_fd, 0);
		if (ram == MAP_FAILED) {
			err(EXIT_FAILURE, "client: mmap");
		}
		close(uvmem_fd);
	}
#else
	// mapped client range
	void *ram = mmap((void*)(HOST_ADDR_START+GUEST_ADDR_START), uvmem_size, PROT_READ | PROT_WRITE, MAP_PRIVATE|MAP_FIXED,
	 			uvmem_fd, 0);
	if (ram == MAP_FAILED) {
		err(EXIT_FAILURE, "client: mmap");
	}

	pthread_t server_thread;
	pthread_attr_t attr; int rc;
	struct uvmem_server_arg sarg;
	sarg.fp = fp;
	sarg.uvmem_fd = uvmem_fd;
	sarg.shmem_fd = shmem_fd;
	sarg.size = uvmem_size;
	sarg.page_size = page_size;
	
	rc = pthread_attr_init(&attr);
	if (rc) 
		err(EXIT_FAILURE, "pthread_attr_init");
	rc = pthread_create(&server_thread, &attr, serve_uvmem_pages_thread, &sarg);
	debug_printf("Thread id %u\n", (unsigned int)server_thread);
	if (rc)
		err(EXIT_FAILURE, "pthread_create");

#endif
}

// FILE *fp_guest_mem;

void ZoogVM::_load_swap(const char *core_file, struct swap_vm **out_vm)
{
	set_swapfile(core_file);
	// const char *corefile = "kvm.core";
	// const char *swapfile = "kvm.swap";

	debug_printf("Resume from core file: %s\n", core_file);

	// FILE *fp = fopen(corefile, "r");
	// lite_assert(fp!=NULL);

	FILE *fp_swap = fopen(swapfile, "r");
	lite_assert(fp_swap!=NULL);

	FILE *fp_page = fopen(pagefile, "r");
	// lite_assert(fp_page!=NULL);

	// CoreFile c;
	// corefile_read(fp, &c);
	int rc; 
	// int pos = 0; // track the last phdr was read
	int i, j;
	// int num_notes = 1;
	// int thread_notes_size;
	// int thread_offset;
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

	// Elf32_Ehdr ehdr;
	// rc = fread(&ehdr, sizeof(Elf32_Ehdr), 1, fp);
	// lite_assert(rc == 1);
	// pos += sizeof(ehdr);
	// lite_assert(pos == ftell(fp));

	// // Read CoreNotes
	// Elf32_Phdr phdr;
	// rc = fread(&phdr, sizeof(Elf32_Phdr), 1, fp);
	// lite_assert(rc == 1);
	// pos += sizeof(phdr);
	// lite_assert(pos == ftell(fp));
	// thread_notes_size = phdr.p_filesz - sizeof(CoreNote_Zoog);
	// thread_offset = phdr.p_offset;

	// int mem_header_count = ehdr.e_phnum - num_notes;
	int mem_header_count = header.segment_count;
	if (DYNAMIC_MAPPER)
	{
		mmapper_list = (Mmapper*)mf_malloc(mf, sizeof(Mmapper) * mem_header_count);
		mmaper_list_count = mem_header_count;
		// set_segv_handler();

		// {
		// 	fp_guest_mem = fopen("/elasticity/embassies/nginx.mem", "rw");
		// 	lite_assert(fp_guest_mem!=NULL);
		// 	void *mmap_rc = mmap((void*)0x28001000, 69763072, PROT_READ|PROT_WRITE, 
		// 		MAP_PRIVATE|MAP_FILE|MAP_FIXED, fileno(fp_guest_mem), 0);
		// 	lite_assert(mmap_rc == (void*)0x28001000);
		// }
	}

	// Begin loading memory pages
	struct swap_segment seg;
	uint32_t last_guest_range_end = GUEST_ADDR_START; // the same as _map_physical_memory
	for (i = 0; i < mem_header_count; i++)
	{
		// rc = fread(&phdr, sizeof(Elf32_Phdr), 1, fp);
		// lite_assert(rc == 1);
		// pos += sizeof(phdr);
		// lite_assert(pos == ftell(fp));

		rc = fread(&seg, sizeof(seg), 1, fp_swap);
		lite_assert(rc == 1);
		
		{
			// uint32_t size = phdr.p_memsz;
			// uint32_t vaddr = phdr.p_vaddr;
			// uint32_t foffset = phdr.p_offset;
			uint32_t size = seg.size;
			uint32_t vaddr = seg.vaddr;
			uint32_t foffset = seg.offset;
			
			
			MemSlot *mem_slot = new MemSlot(label);
			Range guest_range;
			uint32_t round_size = size;
			guest_memory_allocator->allocate_range_at(vaddr, round_size, mem_slot, &guest_range);
			uint8_t *host_addr = host_phys_memory + guest_range.start;
			mem_slot->configure(guest_range, host_addr);

			int mmap_prot = PROT_READ|PROT_WRITE;
			if (DYNAMIC_MAPPER)
			{
				mmap_prot = PROT_NONE;
				mmapper_list[i].host_addr = host_addr;
				mmapper_list[i].size = round_size;
				mmapper_list[i].fp_offset = foffset;
				mmapper_list[i].virt_addr = vaddr;
				mmapper_list[i].prot = mmap_prot;
			}

			if (!DYNAMIC_MAPPER) {
				void *mmap_rc = mmap(host_addr, round_size, mmap_prot, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
				lite_assert(mmap_rc == host_addr);
				
				fseek(fp_page, foffset, SEEK_SET);
				fread(host_addr, size, 1, fp_page);
				lite_assert((int)(foffset + size) == ftell(fp_page));
			}

			debug_printf("Memory loaded: get_guest_addr=%x, get_host_addr=%x, get_size=%d\n", 
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
			// 3/11/15, should be the case. Allocated ranges are saved in an 
			// AVL tree (balanced tree). When checkpointing, ranges read in 
			// ascending order.
			lite_assert(guest_range.start >= last_guest_range_end);
			if (guest_range.start > last_guest_range_end) {
				// there is a gap, let's make a free block
				guest_memory_allocator->force_insert_coalesced_free_range(
					Range(last_guest_range_end, guest_range.start));
			}
			last_guest_range_end = guest_range.end;

			// fseek(fp, pos, SEEK_SET);
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

	if (DYNAMIC_MAPPER) {
		init_uvmem(this, fp_page, last_guest_range_end);
	}

	// End of memory loading

	// while (pos < thread_offset + thread_notes_size)
	// {
	// 	CoreNote_Regs corenote_regs;
	// 	rc = fread(&corenote_regs, sizeof(CoreNote_Regs), 1, fp);
	// 	ZoogVCPU *vcpu = new ZoogVCPU(this, corenote_regs.desc.regs.ip, corenote_regs.desc.regs.sp);
	// 	vcpu->get_zid();
	// }

	struct swap_thread_extra *ptr;
	for(ptr = ptr_thread; ptr < ptr_thread + header.thread_count; ptr++) {
		debug_printf("Thread: guest_entry_point=%x, stack_top_guest=%x, gdt_page_guest_addr=%x\n", 
			ptr->thread.guest_entry_point, ptr->thread.stack_top_guest, ptr->thread.gdt_page_guest_addr);
		ZoogVCPU *vcpu = new ZoogVCPU(this, ptr);
		vcpu->get_zid(); 
		lite_assert(ptr->gdt_page);
	}

	// if (DYNAMIC_MAPPER) {
	// 	// segv_load_all();
	// 	void *desired_host_phys_addr = (void*) HOST_ADDR_START;
	// 	rc = mprotect(desired_host_phys_addr, host_phys_memory_size, PROT_NONE);
	// 	lite_assert(rc==0);
	// 	// segv_load_addr((void*)0x28003000);
	// 	// segv_load_addr((void*)0x28153000);	
	// }

	// if (!DYNAMIC_MAPPER) {
	// 	fclose(fp_page);
	// }
	fclose(fp_swap);
}
