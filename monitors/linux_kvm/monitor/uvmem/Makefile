ifneq ($(KERNELRELEASE),)
# kbuild part of makefile
obj-m  := uvmem.o

else
# normal makefile
KDIR ?= /lib/modules/`uname -r`/build

SYSMAP_SYMBOLS = shmem_zero_setup

uvmem_sysmap.h :
	/bin/bash make_uvmem_sysmap.sh $(SYSMAP_SYMBOLS) > $@

uvmem.ko : uvmem_sysmap.h uvmem.c uvmem.h
	$(MAKE) -C $(KDIR) M=$$PWD

build : uvmem.ko

insmod : uvmem.ko
	sudo insmod uvmem.ko

rmmod :
	sudo rmmod uvmem

chown :
	sudo chown `id -u` /dev/uvmem

all : build
.PHONY : all clean build rmmod chown insmod

testbin :
	gcc -g -o uvmem_test uvmem_test.c -lpthread

test : testbin
	./uvmem_test

clean :
	rm *.o *.ko *.mod.c *.symvers *.order uvmem_test uvmem_sysmap.h

endif
