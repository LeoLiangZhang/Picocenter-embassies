# commands

## Environment variable 
ZOOG_TUNID=1

Following commands should be ran in chroot'ed environment. 

## terminal-1
```
cd ~/Works/embassies/monitors/linux_dbg
ZOOG_TUNID=1 ./monitor/build/xax_port_monitor

cd ~/Works/embassies/monitors/linux_kvm
ZOOG_TUNID=2 ./coordinator/build/zoog_kvm_coordinator
```

## terminal-2
```
cd ~/Works/embassies
ZOOG_TUNID=1 ./toolchains/linux_elf/zftp_backend/build/zftp_backend --origin-filesystem true --origin-reference true --listen-zftp tunid --listen-lookup tunid --index-dir zftp_index_origina
# or
ZOOG_TUNID=2 ./toolchains/linux_elf/zftp_backend/build/zftp_backend --origin-filesystem true --origin-reference true --listen-zftp tunid --listen-lookup tunid --index-dir zftp_index_originb
```


## terminal-3
```
cd ~/Works/embassies

ZOOG_TUNID=1 ./monitors/linux_dbg/pal/build/xax_port_pal toolchains/linux_elf/elf_loader/build/elf_loader.lion.signed

ZOOG_TUNID=2 ./monitors/linux_kvm/monitor/build/zoog_kvm_monitor --image-file ./toolchains/linux_elf/elf_loader/build/elf_loader.lion.signed --wait-for-core false
```

## run without *schroot*

schroot -d ~/Works/embassies/monitors/linux_kvm/monitor/ -- make
schroot -d ~/Works/embassies/monitors/linux_kvm/coordinator/ -- make
schroot -d ~/Works/embassies/monitors/linux_kvm/ -- make

schroot -d ~/Works/embassies/monitors/linux_dbg/monitor/ -- make
schroot -d ~/Works/embassies/monitors/linux_dbg/pal/ -- make
schroot -d ~/Works/embassies/monitors/linux_dbg/ -- make

schroot -d ~/Works/embassies/toolchains/linux_elf/lion/ -- make

### DBG mode

schroot -- bash -c "export DISPLAY=$DISPLAY ; cd ~/Works/embassies/monitors/linux_dbg/ && ZOOG_TUNID=1 ./monitor/build/xax_port_monitor"

schroot -- bash -c "export DISPLAY=$DISPLAY ; cd ~/Works/embassies/ && ZOOG_TUNID=1 ./toolchains/linux_elf/zftp_backend/build/zftp_backend --origin-filesystem true --origin-reference true --listen-zftp tunid --listen-lookup tunid --index-dir zftp_index_origina"

schroot -- bash -c "cd ~/Works/embassies/ && ZOOG_TUNID=1 ./monitors/linux_dbg/pal/build/xax_port_pal toolchains/linux_elf/elf_loader/build/elf_loader.lion.signed"

### KVM mode

schroot -- bash -c "export DISPLAY=$DISPLAY ; cd ~/Works/embassies/monitors/linux_kvm/ && ZOOG_TUNID=2 ./coordinator/build/zoog_kvm_coordinator"

schroot -- bash -c "export DISPLAY=$DISPLAY ; cd ~/Works/embassies/ && ZOOG_TUNID=2 ./toolchains/linux_elf/zftp_backend/build/zftp_backend --origin-filesystem true --origin-reference true --listen-zftp tunid --listen-lookup tunid --index-dir zftp_index_originb"

schroot -- bash -c "cd ~/Works/embassies/ && ZOOG_TUNID=2 ./monitors/linux_kvm/monitor/build/zoog_kvm_monitor --image-file ./toolchains/linux_elf/elf_loader/build/elf_loader.lion.signed --wait-for-core false"


