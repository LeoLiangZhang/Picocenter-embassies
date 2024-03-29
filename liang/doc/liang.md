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

**Recently updated options for the monitor**

ZOOG_TUNID=2 /elasticity/embassies/monitors/linux_kvm/monitor/build/zoog_kvm_monitor --image-file /elasticity/embassies/toolchains/linux_elf/elf_loader/build/elf_loader.nginx.signed --wait-for-core false --swap-file kvm.swap --assign-in-address 10.2.0.5 --pico-id 42 --resume 

** on Ravello **
ZOOG_TUNID=2 /elasticity/embassies/monitors/linux_kvm/monitor/build/zoog_kvm_monitor --image-file /elasticity/embassies/toolchains/linux_elf/elf_loader/build/elf_loader.nginx.signed --wait-for-core false --swap-file kvm.swap --assign-in-address 10.2.0.5 --pico-id 42 --resume


## Checkpointing

pkill --signal SIGUSR2 'zoog_kvm_mon'

## Resume

schroot -- bash -c "cd /elasticity/embassies/ && ZOOG_TUNID=2 ./monitors/linux_kvm/monitor/build/zoog_kvm_monitor --core-file kvm.swap --wait-for-core false"

wait-for-core ::= ture | false  # request coredump when error

ZOOG_TUNID=2 ./monitors/linux_kvm/monitor/build/zoog_kvm_monitor --wait-for-core false --swap-file kvm.swap --resume

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

## Iptables for Tun SNAT

iptables -A POSTROUTING -t nat -o eth0 -s 10.2.0.0/16 -d  0/0 -j MASQUERADE
iptables -A FORWARD -t filter -o eth0 -m state --state NEW,ESTABLISHED,RELATED -j ACCEPT
iptables -A FORWARD -t filter -i eth0 -m state --state ESTABLISHED,RELATED -j ACCEPT

Assuming root privilege, eth0 is the Internet interface and Tun is working on 10.2.0.0/16.

# profile

works in pico/<pico_id>/ folder

s3put -b elasticity-storage -p /elasticity/embassies/var/worker/ kvm.swap kvm.swap.page

python -m line_profiler kvm_monitor_uvmem_helper.lprof > profile.s3.txt

ZOOG_TUNID=2 /elasticity/embassies/monitors/linux_kvm/monitor/build/zoog_kvm_monitor --image-file /elasticity/embassies/toolchains/linux_elf/elf_loader/build/elf_loader.lion.signed --wait-for-core false --swap-file kvm.swap --assign-in-address 10.2.0.5 --pico-id 42 --resume

ln -s /elasticity/embassies/monitors/linux_kvm/scripts/kvm_monitor_uvmem_helper.py .


# Worker commander
worker.pico_exec(5, '10.2.0.5', '192.168.1.50:8080.TCP=10.2.0.5:8080', False)
worker.pico_exec(6, '10.2.0.6', '192.168.1.50:8081.TCP=10.2.0.6:8080', False)

# Config related files

coreswap.h - My resume structure
linux_kvm_protocol.h - KVM related structure, VCPU control struct.

# Extra dependencies

## Python

Install these in schroot with pip, e.g., sudo pip install ...

* pyzmq - ZeroMQ python binding
* ordereddict - use by LRU cache implementation 
* boto - python API to AWS
* tornado (v4.1) - use IOLoop and its async framework

sudo pip install ordereddict boto pyzmq tornado
sudo pip install line_profiler

## System

Install in schroot.

net-tools - ifconfig utils
procps - ps pgrep pkill etc.

/etc/apt/sources.list
deb http://ftp.debian.org/debian squeeze non-free

sudo apt-get install python-profiler

schroot -- sudo apt-get install python2.6-dev iptables curl python-pip net-tools procps

## Ravello deployment

sudo apt-get install build-essential

# Deployment

sudo ln -s /elasticity/embassies/worker/embassies_iptables_rsyslog.conf /etc/rsyslog.d/embassies_iptables_rsyslog.conf

# basic datastructure

In `/common/utils/`

- hash_table.h
- linked_list.c


# HTTPD/apache compiliation

(squeeze_i386)liang@neutron:httpd-2.0.65$ LDFLAGS=-pie ./configure --prefix=/home/liang/Works/embassies/toolchains/linux_elf/apache --enable-cache --enable-mem-cache


# Random bits

Install CRIU (1.4) then run its check command, output are shown below:

liang@neutron:criu-1.4$ sudo ./criu check
[sudo] password for liang: 
prctl: PR_SET_MM_MAP is not supported, which is required for restoring user namespaces
Error (timerfd.c:56): timerfd: No timerfd support for c/r: Inappropriate ioctl for device
Error (cr-check.c:308): fdinfo doesn't contain the mnt_id field

ssh -i ~/.ssh/id_rsa ubuntu@ubuntu1404kvm-embassieskvmonaws-ownzaq3s.srv.ravcloud.com
