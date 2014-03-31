
########## Variables

ZOOG_ROOT=../../..

TARGETS=\
	lion-pie \

H_SRCS=\
	lion.cpp \

MODULES=
MODULE_OBJS=

H_OBJS=$(call mapSrcsToObjs, $(H_SRCS) $(MODULE_OBJS))

DEP_SRCS=$(H_SRCS)

INCS=\
	-I$(ZOOG_ROOT)/common/crypto \
	-I$(ZOOG_ROOT)/common/ifc \
	-I$(ZOOG_ROOT)/toolchains/linux_elf/common \

CFLAGS=\
	-g -pie -fpie \
	$(INCS) \
	-DZOOG_ROOT='"$(realpath $(ZOOG_ROOT))"' \


########## Include

include ../make.in

########## Rules

$(BUILD)/lion-pie: $(H_OBJS)
	$(CXX) -pie $(CFLAGS) $^ -o $@ -lm

