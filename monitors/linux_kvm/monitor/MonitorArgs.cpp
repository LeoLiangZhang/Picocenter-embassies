#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "args_lib.h"
#include "MonitorArgs.h"

MonitorArgs::MonitorArgs(int argc, char **argv)
{
	delete_image_file = false;
	// image_file = ZOOG_ROOT "/toolchains/linux_elf/paltest/build/paltest.raw";
	image_file = ZOOG_ROOT "/toolchains/linux_elf/elf_loader/build/elf_loader.vendor_a.signed";
	wait_for_debugger = false;
	wait_for_core = true;
	swap_file = "kvm.swap";
	is_resume = false;

	// in_address = NULL;
	// in_gateway = NULL;
	// in_netmask = NULL;
	// in6_address = NULL;
	// in6_gateway = NULL;
	// in6_netmask = NULL;
	assign_in_address = NULL;

	argc-=1; argv+=1;

	while (argc>0)
	{
		CONSUME_BOOL("--delete-image-file", delete_image_file);
		CONSUME_STRING("--image-file", image_file);
		CONSUME_BOOL("--wait-for-debugger", wait_for_debugger);
		CONSUME_BOOL("--wait-for-core", wait_for_core);
		CONSUME_STRING("--swap-file", swap_file);
		CONSUME_OPTION("--resume", is_resume);

		// CONSUME_STRING("--in-address", in_address);
		// CONSUME_STRING("--in-gateway", in_gateway);
		// CONSUME_STRING("--in-netmask", in_netmask);
		// CONSUME_STRING("--in6-address", in6_address);
		// CONSUME_STRING("--in6-gateway", in6_gateway);
		// CONSUME_STRING("--in6-netmask", in6_netmask);
		CONSUME_STRING("--assign-in-address", assign_in_address);

		char buf[500]; snprintf(buf, sizeof(buf), "Unknown argument: %s", argv[0]);
		check(false, buf);
	}
}
