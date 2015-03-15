Picocenter System Design

# The hub

## Data Structures

+ WorkerTable

  (workder_id, status:WorkerStatus, public_ips:List)

+ WorkerStatus enum
  - CanAcceptPico
  - Overload
  - Offline

+ AllocatedPortsTable

  (ip, ports:List)

+ DnsMappingTable

  (hostname, public_ip)

+ PicoTable

  (pico_id, worker_id, internal_ip, hostname, public_ip_port_proto_internal_port:list)
  Notes:
  public_ip_port_proto_internal_port is a list of tuple:
  public_ip:port - protocol - internal_port

+ PicoExitStatus
  - Success
  - Fail

+ PicoStartMeta:JSON
  - requested_ports: [port...]
  - exe_path

## Functions

External user functions:
+ new_pico(file_tar, meta:PicoStartMeta):PicoId
+ kill_pico(pico_id)

Worker related functions:
+ update_worker_status(status:WorkerStatus)
+ pico_release(pico_id)

  The picoprocess has been swapped off. The worker holds no locks on the picoprocess. The hub reclaims ips and ports resources. When new incoming DNS request comes to the picoprocess, the hub finds a worker, and calls pico_exec with PicoExecFlag.Resume.

+ pico_exit(pico_id, exit_status:PicoExitStatus)

# The worker

## Data structures

+ PicoExecFlag
  - None = 0
  - Resume = 1

## Functions

+ pico_exec(pico_id, flags:PicoExecFlag)
+ pico_kill(pico_id)
