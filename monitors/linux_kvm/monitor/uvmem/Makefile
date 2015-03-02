ifneq ($(KERNELRELEASE),)
# kbuild part of makefile
obj-m  := uvmem.o

else
# normal makefile
KDIR ?= /lib/modules/`uname -r`/build

default:
	$(MAKE) -C $(KDIR) M=$$PWD

# Module specific targets
genbin:
	echo "X" > uvmem.o_shipped

testbin:
	gcc -g -o uvmem_test uvmem_test.c

test: testbin
	./uvmem_test

clean:
	rm *.o *.ko *.mod.c *.symvers *.order uvmem_test

endif
