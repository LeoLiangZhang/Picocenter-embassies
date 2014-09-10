#include "RunConfig.h"
#include "Zone.h"

void config_create(RunConfig *rc)
{
  // Zone *zone;
  // fprintf(stderr, "Hello world 2\n");
  Zone *zone = new Zone("lighttpd",	// just a label for debugging inside zguest
  			20,			// MB of brk()-accessible address space.
  			ZOOG_ROOT "/toolchains/linux_elf/lighttpd/sbin/lighttpd"
  			// path to the -pie binary you want to run.
  			);
  zone->add_arg("-f");
  zone->add_arg("/home/liang/Works/embassies/toolchains/linux_elf/lighttpd/conf/lighttpd.conf");
  rc->add_zone(zone);
  
  // rc->add_vnc();
  // zone = rc->add_app("hello");

}