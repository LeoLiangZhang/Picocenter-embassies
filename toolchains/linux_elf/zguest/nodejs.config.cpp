#include "RunConfig.h"
#include "Zone.h"

void config_create(RunConfig *rc)
{
  // Zone *zone;
  // fprintf(stderr, "Hello world 2\n");
  Zone *zone = new Zone("nodejs",	// just a label for debugging inside zguest
  			20,			// MB of brk()-accessible address space.
  			ZOOG_ROOT "/toolchains/linux_elf/liang_apps/nodejs/bin/node"
  			// path to the -pie binary you want to run.
  			);
  zone->add_arg(ZOOG_ROOT "/pseudofiles/main.js");
  rc->add_zone(zone);
  
  // rc->add_vnc();
  // zone = rc->add_app("hello");

}