#include "RunConfig.h"
#include "Zone.h"

void config_create(RunConfig *rc)
{
  // Zone *zone;
  // fprintf(stderr, "Hello world 2\n");
  Zone *zone = new Zone("my_hello",	// just a label for debugging inside zguest
  			10,			// MB of brk()-accessible address space.
  			ZOOG_ROOT "/toolchains/linux_elf/lion/build/lion-pie"
  			// path to the -pie binary you want to run.
  			);
  rc->add_zone(zone);
  
  // rc->add_vnc();
  // zone = rc->add_app("hello");

}


