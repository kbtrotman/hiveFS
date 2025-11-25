# hiveFS
A Hive Mind Filesystem

HiveFS is hive mind filesystem for physical Linux, (Windows?) and VMWare/Nutanix/Cloud datastore.

(This project is still in development.)

What is a hive mind filesystem? There is no better or more descriptive way to describe HiveFS than
in saying it is a full hypervisor that lives in the O/S layer for backend storage. Everything is
virtualized now. Why not the filesystem too? HiveFS is virtual in the sense that the local kernel 
believes it is mounting physical storage, while in reality, it is actually managing any number of 
remotely managed filesystems, all completely virtual. But we're getting ahead of ourselves. See,
this is somewhat of a bold statemnt, but it comes from what HiveFS actually is. Clustered filesystems
use raft consensus to write data and control locking. In the engineering world, Software Defined
storage is a superset of clustered filesystems which enables some vitualized components. This is 
what HiveFS is, software defined storage, but in addition, it is slightly more unusual than 
traditional SDS in that its main virtualization component is a kernel driver that tricks the O/S 
kernel into thinking it's only one local disk, when it's actually many remote network-based 
filesystems in the same style as traditional SDS. On the backend, all data is stored in one global 
virtual de-dupe pool. In addition, SDS usually is a single tree design shared between many servers. 
HiveFS has a global tree with branches that represent shared filesystems, hosts, and other virtual 
entities. (This layer doesn't actually exist anywhere except conceptually.) Under hosts, a 
filesystem may be dedicated to that host, shared between many hosts, or junctioned partially from 
one host to another. No data has to be SAN mounted. It's recommended to have a small local cache on 
SSD disk to make sure remote speed is no slower than local speed. Traditional SDS has a single global 
cache, while HiveFS's local client cache makes a large percentage of reads extremely fast. This pairs
well with SSD, which is tuned for high-speed writes to make a well-rounded performance profile. These 
unique traits don't mean that HiveFS is not software defined storage, but rather, they simply mean 
its a different architecture of software defined storage than other, more object-based models. (The 
closest other comparisons would be primarily object style clustered filesystems.) Once storage is 
attached to the backend nodes, whether local commodity or SAN-attached arrays, all management is 
done through one interface. In fact, the point of HiveFS's design is simple ease of management for 
storage architects. HiveFS is both clustered and SDS storage like many virtualization models coming 
out currently, but unlike many virtual systems, it treats the storage as block-only by the internal 
protocol, like any local filesystem, but with an additional node or nodes, it can also provide file 
and object stores (see below). It was not originally designed to do everything internally, however. 
It focuses on doing one thing well, block storage over network.

There are 3 required and one optional companents to the "Hive" in general:
1. The central "Hive" is one or more server(s), storage appliances, installed via ISO image. 
   The ISO install will setup an embedded linux system and configure all storage behind the node
   for use as a blob of storage in the hive. From that point the storage can be managed without
   any traditional storage management (such as zoning, lun management, etc) since it is a
   virtual filesystem. All management is done from the hive node's GUI or the client host CLI.
   In fact, when using commodity storage, locally attached, there is no management outside the
   initial ISO install and configuration. Adding nodes adds both the ability to attach more storage 
   via PCI slots/internal bays and distribution of incoming reads/writes for more bandwidth. The 
   hive runs a software called Hive_Guard which provides a whole host of functions, from securing
   incoming transmissions, balancing erasure code stripes across the nodes, re-balancing EC when 
   nodes are added/deleted, raft clustering algorythm, replicated write logs, and managing Posix 
   locks, leases, and notifications. Hive guard acts as a query distribution and load balances all
   load across every node equally. And by using erasure coding, it not only provides redundancy but
   also breaks up each block into chunks written across the nodes for very efficient write speeds.
   Each written block is distributed across six nodes, which significantly increases speed. (However,
   two of those stripes are parity, meaning with 6 nodes or more, the system can loose at least two 
   nodes and operate degradaded, but still retain viability.) Backend storage can be anything that 
   appears locally attached but the hive is tuned specifically for SSD (especially NVME). HSD is not 
   recommended. It uses a high-speed key/value store for de-dupe that is extremely fast as configured
   and backed by SSD.

2. HiveFS is a kernel module that takes a small local disk and builds a superblock on it and cache.
   The superblock allows the FS to load and then points it to the central hive for inode data. A small 
   configurable cache locally on the disk acts as NVRAM on a storage array acts. In other words, in 
   a power outage, the local cache keeps data consistent. Both the superblock and NVRAM area can be 
   very small, on the order of less than 1GB for typical use. Cache is not strictly required, but
   without it there is a risk of data loss and a slight speed loss. Note that this cache mounts as
   a filesystem would and makes the kernel think its a reall filesystem. However, if you do a df -k,
   it will not show up as a filesystem, and if you attempt to list the directory, you'll be told
   that function isn't supported. See, the cache is just a collection of most-often used directories,
   inodes, and blocks. It has no tree structure or ability to be read locally. This makes high-speed
   reads very fast. Unlike traditional clusters and most SDS platforms, HiveFS is not architected so
   that storage nodes are also the only end clients. In fact, storage nodes in the hive are not
   designed to be clients at all. Instead, end clients participate in fast reads/writes due to their
   not having any storage based in the shared filesystem. They only have local cache tuned for fast
   reads. Writes are done over network topology to the hive storage nodes.

3. Hi_Command is a user-space program that runs on the same linux client as HiveFS. Hi_Command sets 
   up FIFO queues with the kernel module and accepts data from the kernel to be sent over ethernet to 
   the central hive. In other words, its a user space program that implements the protocol necessary 
   to communicate. Using insecure network comms from the linux kernel isn't a good idea. By moving this
   to user space, it can use TLS encryption and higher-level token authentication, to pair with 
   Hive_Guard on the remote hive. Which node it attaches to doesn't matter, because Hive_guard will
   direct traffic and provide the data in the lowest latency method it can.

4. HoneyComb is an optional node server install ISO that can be added which has the hive software to
   access the datastore, the hiveFS kernel module, and hi_command installed. It also has Hive_guard 
   installed can access storage high speed and provide a front-end for NFS or webdav protocols for use 
   in physical or cloud infrastructure. This extendeds the inherent block archtecture of the hive to 
   file and object storage. With hive_guard, it can also act as a traffic director when necessary or 
   honeycomb and any other hive node can also act as the GUI access point where you manage the entire
   setup. Since the Honeycomb runs the hive software, it can also act as a full particupating member 
   of the hive and accept client communication to store or read data. However, if file and object 
   traffic is heavy, it will hand off communication from hive clients as load gets higher and 
   prioritize its own function first.


Like any virtualization, HiveFS is designed by architecture to easily expand across a hybrid cloud or 
multi-cloud setup by using local or cloud-vendor based SSD block storage and the hive can be installed 
into virtual machines in any cloud. The clients do not need to be local to the hive install, however, 
to avoid storage timeouts they should have a link with reasonable communication speed.

And again, regardless of O/S, application, or other factors, hiveFS is designed to be globally de-dupable 
across all volumes, hosts, and backend storage. In addition some compression is applied. However, because 
keeping only one copy of every block of data is a very bad idea, the hive is designed to be at least 3 nodes 
with the data split into 3 copies. It does support a single node or dual-node (with replication to the second 
node). However, whether you have 3 nodes or twenty, at least 3 copies of all data will be kept by doing 
split-writes across the nodes (the number of copies is cofigurable from 3+ or replication for 2). Single copy 
is possible for small global filesystems and supported but carries the risk of loss of data, so is prefereable
only for test environments.

File versioning. A file can have a configurable number of historical copies as a backup method to go back to 
previous versions of that file/dir. Replication covers large-scale outages, so traditional backup is built-in 
to the filesystem architecture by the way these two functions behave. Further, since a directory structure can 
be modified on the fly and new mounts advertised, it supports the same live mount function as today's CDM-based 
software via the Honeycomb server (by cloning a dir/filesystem and mounting to the desired host(s)).

(Note that the more file versioning copies are kept, the more old data blocks must stay around and not cleaned.)

Extended ACLs and extended file meta-data (for use in web-based file tagging) is intended to be supported.

Clustered access. Because all storage is contained in one global filesystem, hosts can have their own filesystem 
or share filesystems with other hosts. In other words, the global filesystem is layed out like this:

![alt text](assets/ex_layout.png)

Since it's a global filesystem, so long as permissions allow, you can junction any dirs whatever machines you 
like and they become either subdirectories of existing mounts or are advertised as new filesystems to be mounted. 
You can have a single host as owner of an FS to allow the host admin to control permissions, or you can allow the 
hive to own a directory as a shared filesystem with permissions controlled by the hive admin. Even dirs owned
by only one host, such as mount1 by machine1 and machine2 above, are still de-duped against each other.
