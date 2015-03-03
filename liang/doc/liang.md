# commands

## Environment variable 
ZOOG_TUNID=1

## Run with *schroot*

To enter chroot'ed environment, just type "schroot"

### BUILD

#### KVM
schroot -d /elasticity/embassies/monitors/linux_kvm/monitor/ -- make
schroot -d /elasticity/embassies/monitors/linux_kvm/coordinator/ -- make
schroot -d /elasticity/embassies/monitors/linux_kvm/ -- make

#### DBG
schroot -d /elasticity/embassies/monitors/linux_dbg/monitor/ -- make
schroot -d /elasticity/embassies/monitors/linux_dbg/pal/ -- make
schroot -d /elasticity/embassies/monitors/linux_dbg/ -- make

#### Applications
schroot -d /elasticity/embassies/toolchains/linux_elf/lion/ -- make

### DBG mode

schroot -- bash -c "export DISPLAY=$DISPLAY ; cd /elasticity/embassies/monitors/linux_dbg/ && ZOOG_TUNID=1 ./monitor/build/xax_port_monitor"

schroot -- bash -c "export DISPLAY=$DISPLAY ; cd /elasticity/embassies/ && ZOOG_TUNID=1 ./toolchains/linux_elf/zftp_backend/build/zftp_backend --origin-filesystem true --origin-reference true --listen-zftp tunid --listen-lookup tunid --index-dir zftp_index_origina"

schroot -- bash -c "cd /elasticity/embassies/ && ZOOG_TUNID=1 ./monitors/linux_dbg/pal/build/xax_port_pal toolchains/linux_elf/elf_loader/build/elf_loader.lion.signed"

### KVM mode

schroot -- bash -c "export DISPLAY=$DISPLAY ; cd /elasticity/embassies/monitors/linux_kvm/ && ZOOG_TUNID=2 ./coordinator/build/zoog_kvm_coordinator"

schroot -- bash -c "export DISPLAY=$DISPLAY ; cd /elasticity/embassies/ && ZOOG_TUNID=2 ./toolchains/linux_elf/zftp_backend/build/zftp_backend --origin-filesystem true --origin-reference true --listen-zftp tunid --listen-lookup tunid --index-dir zftp_index_originb"

schroot -- bash -c "cd /elasticity/embassies/ && ZOOG_TUNID=2 ./monitors/linux_kvm/monitor/build/zoog_kvm_monitor --image-file ./toolchains/linux_elf/elf_loader/build/elf_loader.lion.signed --wait-for-core false"


## Checkpointing

pkill --signal SIGUSR2 'zoog_kvm_mon'

## Resume

schroot -- bash -c "cd /elasticity/embassies/ && ZOOG_TUNID=2 ./monitors/linux_kvm/monitor/build/zoog_kvm_monitor --core-file kvm.swap --wait-for-core false"

wait-for-core ::= ture | false  # request coredump when error

## GDB: debugging in chroot'ed env

cd /elasticity/embassies/
ZOOG_TUNID=2 gdb -c core --args ./monitors/linux_kvm/monitor/build/zoog_kvm_monitor --core-file kvm.swap --wait-for-core false

### in GDB, run these to load symbols

// should remove addyms first
shell /elasticity/embassies/toolchains/linux_elf/scripts/debuggershelper.py
source addsyms

// or run pico_symbols, see .gdbinit
pico_symbols


## Find in source code (without toolchains/linux_elf/apps)

find common monitors toolchains -path toolchains/linux_elf/apps -prune -o \( -name "*.[chS]" -o -name "*.cc" -o -name "*.cpp" \) -print |xargs grep -ine 'Packet' 2>/dev/null


# Config related files

coreswap.h - My resume structure
linux_kvm_protocol.h - KVM related structure, VCPU control struct.

# basic datastructure

In `/common/utils/`

- hash_table.h
- linked_list.c


# HTTPD compiliation

(squeeze_i386)liang@neutron:httpd-2.0.65$ LDFLAGS=-pie ./configure --prefix=/home/liang/Works/embassies/toolchains/linux_elf/apache --enable-cache --enable-mem-cache


# Random bits

Install CRIU (1.4) then run its check command, output are shown below:

liang@neutron:criu-1.4$ sudo ./criu check
[sudo] password for liang: 
prctl: PR_SET_MM_MAP is not supported, which is required for restoring user namespaces
Error (timerfd.c:56): timerfd: No timerfd support for c/r: Inappropriate ioctl for device
Error (cr-check.c:308): fdinfo doesn't contain the mnt_id field

