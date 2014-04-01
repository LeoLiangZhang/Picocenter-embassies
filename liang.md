# commands

ZOOG_TUNID=1

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


# Experiments

dbg:

Send 1000 packets in 543394 microsec, or 543.394000 microsec/packet

Loops: 250000, Iterations: 1, Duration: 14 sec.
C Converted Double Precision Whetstones: 1668.2 MIPS


kvm:

Send 1000 packets in 741849 microsec, or 741.849000 microsec/packet

Loops: 250000, Iterations: 1, Duration: 15 sec.
C Converted Double Precision Whetstones: 1603.2 MIPS